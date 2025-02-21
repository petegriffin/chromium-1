// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/payments/local_card_migration_manager.h"

#include <stddef.h>

#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/guid.h"
#include "base/metrics/metrics_hashes.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_task_environment.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/autofill_metrics.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/autofill/core/browser/mock_autocomplete_history_manager.h"
#include "components/autofill/core/browser/payments/test_credit_card_save_manager.h"
#include "components/autofill/core/browser/payments/test_local_card_migration_manager.h"
#include "components/autofill/core/browser/payments/test_payments_client.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/sync_utils.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/browser/test_autofill_clock.h"
#include "components/autofill/core/browser/test_autofill_driver.h"
#include "components/autofill/core/browser/test_autofill_manager.h"
#include "components/autofill/core/browser/test_form_data_importer.h"
#include "components/autofill/core/browser/test_personal_data_manager.h"
#include "components/autofill/core/browser/validation.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/prefs/pref_service.h"
#include "components/sync/driver/test_sync_service.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_test_util.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using base::ASCIIToUTF16;
using testing::_;

namespace autofill {

class LocalCardMigrationManagerTest : public testing::Test {
 public:
  void SetUp() override {
    autofill_client_.SetPrefs(test::PrefServiceForTesting());
    personal_data_.SetPrefService(autofill_client_.GetPrefs());
    personal_data_.SetSyncServiceForTest(&sync_service_);
    autofill_driver_.reset(new TestAutofillDriver());
    request_context_ = new net::TestURLRequestContextGetter(
        base::ThreadTaskRunnerHandle::Get());
    autofill_driver_->SetURLRequestContext(request_context_.get());
    payments_client_ = new payments::TestPaymentsClient(
        autofill_driver_->GetURLLoaderFactory(), autofill_client_.GetPrefs(),
        autofill_client_.GetIdentityManager(), &personal_data_);
    autofill_client_.set_test_payments_client(
        std::unique_ptr<payments::TestPaymentsClient>(payments_client_));
    credit_card_save_manager_ =
        new TestCreditCardSaveManager(autofill_driver_.get(), &autofill_client_,
                                      payments_client_, &personal_data_);
    credit_card_save_manager_->SetCreditCardUploadEnabled(true);
    local_card_migration_manager_ = new TestLocalCardMigrationManager(
        autofill_driver_.get(), &autofill_client_, payments_client_,
        &personal_data_);
    std::unique_ptr<TestStrikeDatabase> test_strike_database =
        std::make_unique<TestStrikeDatabase>();
    strike_database_ = test_strike_database.get();
    autofill_client_.set_test_strike_database(std::move(test_strike_database));
    autofill::TestFormDataImporter* test_form_data_importer =
        new TestFormDataImporter(
            &autofill_client_, payments_client_,
            std::unique_ptr<CreditCardSaveManager>(credit_card_save_manager_),
            &personal_data_, "en-US",
            std::unique_ptr<LocalCardMigrationManager>(
                local_card_migration_manager_));
    autofill_client_.set_test_form_data_importer(
        std::unique_ptr<TestFormDataImporter>(test_form_data_importer));
    autofill_manager_.reset(new TestAutofillManager(
        autofill_driver_.get(), &autofill_client_, &personal_data_,
        &autocomplete_history_manager_));
    autofill_manager_->SetExpectedObservedSubmission(true);
  }

  void TearDown() override {
    // Order of destruction is important as AutofillManager relies on
    // PersonalDataManager to be around when it gets destroyed.
    autofill_manager_.reset();
    autofill_driver_.reset();

    personal_data_.SetPrefService(nullptr);
    personal_data_.ClearCreditCards();

    request_context_ = nullptr;
  }

  void FormsSeen(const std::vector<FormData>& forms) {
    autofill_manager_->OnFormsSeen(forms, base::TimeTicks());
  }

  void FormSubmitted(const FormData& form) {
    autofill_manager_->OnFormSubmitted(form, false,
                                       SubmissionSource::FORM_SUBMISSION);
  }

  void EditCreditCardFrom(FormData& credit_card_form,
                          const char* name_on_card,
                          const char* card_number,
                          const char* expiration_month,
                          const char* expiration_year,
                          const char* cvc) {
    DCHECK(credit_card_form.fields.size() >= 5);
    credit_card_form.fields[0].value = ASCIIToUTF16(name_on_card);
    credit_card_form.fields[1].value = ASCIIToUTF16(card_number);
    credit_card_form.fields[2].value = ASCIIToUTF16(expiration_month);
    credit_card_form.fields[3].value = ASCIIToUTF16(expiration_year);
    credit_card_form.fields[4].value = ASCIIToUTF16(cvc);
  }

  void AddLocalCreditCard(TestPersonalDataManager& personal_data,
                          const char* name_on_card,
                          const char* card_number,
                          const char* expiration_month,
                          const char* expiration_year,
                          const std::string& billing_address_id,
                          const std::string& guid) {
    CreditCard local_card;
    test::SetCreditCardInfo(&local_card, name_on_card, card_number,
                            expiration_month, expiration_year,
                            billing_address_id);
    local_card.set_record_type(CreditCard::LOCAL_CARD);
    local_card.set_guid(guid);
    personal_data.AddCreditCard(local_card);
  }

  // Set the parsed response |result| for the provided |guid|.
  void SetUpMigrationResponseForGuid(const std::string& guid,
                                     const std::string& result) {
    std::unique_ptr<std::unordered_map<std::string, std::string>> save_result =
        std::make_unique<std::unordered_map<std::string, std::string>>();
    save_result->insert(std::make_pair(guid, result));
    payments_client_->SetSaveResultForCardsMigration(std::move(save_result));
  }

 protected:
  base::test::ScopedTaskEnvironment scoped_task_environment_;
  ukm::TestAutoSetUkmRecorder test_ukm_recorder_;
  TestAutofillClient autofill_client_;
  std::unique_ptr<TestAutofillDriver> autofill_driver_;
  std::unique_ptr<TestAutofillManager> autofill_manager_;
  scoped_refptr<net::TestURLRequestContextGetter> request_context_;
  TestPersonalDataManager personal_data_;
  MockAutocompleteHistoryManager autocomplete_history_manager_;
  syncer::TestSyncService sync_service_;
  base::test::ScopedFeatureList scoped_feature_list_;
  // Ends up getting owned (and destroyed) by TestAutofillClient:
  TestStrikeDatabase* strike_database_;
  // Ends up getting owned (and destroyed) by TestFormDataImporter:
  TestCreditCardSaveManager* credit_card_save_manager_;
  // Ends up getting owned (and destroyed) by TestFormDataImporter:
  TestLocalCardMigrationManager* local_card_migration_manager_;
  // Ends up getting owned (and destroyed) by TestAutofillClient:
  payments::TestPaymentsClient* payments_client_;
};

// Having one local card on file and using it will not trigger migration.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_UseLocalCardWithOneLocal) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  base::HistogramTester histogram_tester;
  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_FALSE(local_card_migration_manager_->LocalCardMigrationWasTriggered());

  // Verify that metrics are correctly logged to the UseOfLocalCard
  // sub-histogram.
  histogram_tester.ExpectTotalCount(
      "Autofill.LocalCardMigrationOrigin.UseOfLocalCard", 0);
}

// Having any number of local cards on file and using a new card will not
// trigger migration.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_UseNewCardWithAnyLocal) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card (but it will not match what we will enter below).
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card (but it will not match what we will enter
  // below).
  AddLocalCreditCard(personal_data_, "Flo Master", "4444333322221111", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_FALSE(local_card_migration_manager_->LocalCardMigrationWasTriggered());
}

// Use one local card with more valid local cards available, will trigger
// migration.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_UseLocalCardWithMoreLocal) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card, so it will trigger migration.
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  base::HistogramTester histogram_tester;
  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_TRUE(local_card_migration_manager_->LocalCardMigrationWasTriggered());

  // Verify that metrics are correctly logged to the UseOfLocalCard
  // sub-histogram.
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.UseOfLocalCard",
      AutofillMetrics::INTERMEDIATE_BUBBLE_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.UseOfLocalCard",
      AutofillMetrics::INTERMEDIATE_BUBBLE_ACCEPTED, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.UseOfLocalCard",
      AutofillMetrics::MAIN_DIALOG_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.UseOfLocalCard",
      AutofillMetrics::MAIN_DIALOG_ACCEPTED, 1);
}

// Using a local card will not trigger migration even if there are other local
// cards as long as the other local cards are not eligible for migration.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_UseLocalCardWithInvalidLocal) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add other invalid local credit cards(invalid card number or expired), so it
  // will not trigger migration.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111112", "11",
                     test::NextYear().c_str(), "1", "guid2");
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::LastYear().c_str(), "1", "guid3");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_FALSE(local_card_migration_manager_->LocalCardMigrationWasTriggered());
}

// Using a server card when any number of local cards are eligible for migration
// will trigger migration.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_UseServerCardWithOneValidLocal) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a masked server credit card whose |TypeAndLastFourDigits| matches what
  // we will enter below.
  CreditCard credit_card(CreditCard::MASKED_SERVER_CARD, "a123");
  test::SetCreditCardInfo(&credit_card, "Flo Master", "1111", "11",
                          test::NextYear().c_str(), "1");
  credit_card.SetNetworkForMaskedCard(kVisaCard);
  personal_data_.AddServerCreditCard(credit_card);
  // Add one valid local credit card, so it will trigger migration
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid1");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  base::HistogramTester histogram_tester;
  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_TRUE(local_card_migration_manager_->LocalCardMigrationWasTriggered());

  // Verify that metrics are correctly logged to the UseOfServerCard
  // sub-histogram.
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.UseOfServerCard",
      AutofillMetrics::INTERMEDIATE_BUBBLE_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.UseOfServerCard",
      AutofillMetrics::INTERMEDIATE_BUBBLE_ACCEPTED, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.UseOfServerCard",
      AutofillMetrics::MAIN_DIALOG_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.UseOfServerCard",
      AutofillMetrics::MAIN_DIALOG_ACCEPTED, 1);
}

// Using a server card will not trigger migration even if there are other local
// cards as long as the other local cards are not eligible for migration.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_UseServerCardWithNoneValidLocal) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a masked credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  CreditCard credit_card(CreditCard::MASKED_SERVER_CARD, "a123");
  test::SetCreditCardInfo(&credit_card, "Flo Master", "1111", "11",
                          test::NextYear().c_str(), "1");
  credit_card.SetNetworkForMaskedCard(kVisaCard);
  personal_data_.AddServerCreditCard(credit_card);
  // Add other invalid local credit cards(invalid card number or expired), so it
  // will not trigger migration.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111112", "11",
                     test::NextYear().c_str(), "1", "guid1");
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::LastYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  base::HistogramTester histogram_tester;
  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_FALSE(local_card_migration_manager_->LocalCardMigrationWasTriggered());

  // Verify that metrics are correctly logged to the UseOfServerCard
  // sub-histogram.
  histogram_tester.ExpectTotalCount(
      "Autofill.LocalCardMigrationOrigin.UseOfServerCard", 0);
}

// Use one local card with more valid local cards available but experiment flag
// is off, will not trigger migration.
TEST_F(LocalCardMigrationManagerTest, MigrateCreditCard_FeatureNotEnabled) {
  // Turn off the experiment flag.
  scoped_feature_list_.InitAndDisableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card.
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_FALSE(local_card_migration_manager_->LocalCardMigrationWasTriggered());
}

// Do not trigger migration if user only signs in.
TEST_F(LocalCardMigrationManagerTest, MigrateCreditCard_SignInOnlyWhenExpOff) {
  scoped_feature_list_.InitWithFeatures(
      // Enabled
      {features::kAutofillCreditCardLocalCardMigration,
       features::kAutofillEnableAccountWalletStorage},
      // Disabled
      {features::kAutofillEnableLocalCardMigrationForNonSyncUser});

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);

  // Mock Chrome Sync is disabled.
  local_card_migration_manager_->ResetSyncState(
      AutofillSyncSigninState::kSignedInAndWalletSyncTransportEnabled);

  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card.
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_FALSE(local_card_migration_manager_->LocalCardMigrationWasTriggered());
}

// Trigger migration if user only signs in and if experiment is enabled.
TEST_F(LocalCardMigrationManagerTest, MigrateCreditCard_SignInOnlyWhenExpOn) {
  scoped_feature_list_.InitWithFeatures(
      // Enabled
      {features::kAutofillCreditCardLocalCardMigration,
       features::kAutofillEnableLocalCardMigrationForNonSyncUser,
       features::kAutofillEnableAccountWalletStorage},
      // Disabled
      {});

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);

  // Mock Chrome Sync is disabled.
  local_card_migration_manager_->ResetSyncState(
      AutofillSyncSigninState::kSignedInAndWalletSyncTransportEnabled);

  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card.
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_TRUE(local_card_migration_manager_->LocalCardMigrationWasTriggered());
}

// Use one local card with more valid local cards available but billing customer
// number is blank, will not trigger migration.
TEST_F(LocalCardMigrationManagerTest, MigrateCreditCard_NoPaymentsAccount) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card.
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_FALSE(local_card_migration_manager_->LocalCardMigrationWasTriggered());
}

// Tests that local cards that match masked server cards do not count as
// migratable.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_LocalCardMatchMaskedServerCard) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a masked server card whose |TypeAndLastFourDigits| matches a local
  // card.
  CreditCard server_card(CreditCard::MASKED_SERVER_CARD, "a123");
  test::SetCreditCardInfo(&server_card, "Flo Master", "1111", "11",
                          test::NextYear().c_str(), "1");
  server_card.SetNetworkForMaskedCard(kVisaCard);
  personal_data_.AddServerCreditCard(server_card);
  // Add a local card whose |TypeAndLastFourDigits| matches a masked server
  // card.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_FALSE(local_card_migration_manager_->LocalCardMigrationWasTriggered());
}

// Tests that local cards that match full server cards do not count as
// migratable.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_LocalCardMatchFullServerCard) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a full server card whose number matches a local card.
  CreditCard server_card(CreditCard::FULL_SERVER_CARD, "a123");
  test::SetCreditCardInfo(&server_card, "Flo Master", "4111111111111111", "11",
                          test::NextYear().c_str(), "1");
  personal_data_.AddServerCreditCard(server_card);
  // Add a local credit card whose number matches a full server card.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_FALSE(local_card_migration_manager_->LocalCardMigrationWasTriggered());
}

// GetDetectedValues() should includes cardholder name if all cards have it.
TEST_F(LocalCardMigrationManagerTest, GetDetectedValues_AllWithCardHolderName) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card with a different cardholder name.
  AddLocalCreditCard(personal_data_, "John Smith", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_TRUE(local_card_migration_manager_->LocalCardMigrationWasTriggered());
  EXPECT_TRUE(local_card_migration_manager_->GetDetectedValues() &
              CreditCardSaveManager::DetectedValue::CARDHOLDER_NAME);
}

// GetDetectedValues() should not include cardholder name if not all cards have
// a cardholder name.
TEST_F(LocalCardMigrationManagerTest,
       GetDetectedValues_OneCardWithoutCardHolderName) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card without card holder name.
  AddLocalCreditCard(personal_data_, "", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");
  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_TRUE(local_card_migration_manager_->LocalCardMigrationWasTriggered());
  EXPECT_FALSE(local_card_migration_manager_->GetDetectedValues() &
               CreditCardSaveManager::DetectedValue::CARDHOLDER_NAME);
}

// GetDetectedValues() should include the existence of a Google Payments
// account.
TEST_F(LocalCardMigrationManagerTest,
       GetDetectedValues_IncludeGooglePaymentsAccount) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_TRUE(local_card_migration_manager_->LocalCardMigrationWasTriggered());
  EXPECT_TRUE(
      local_card_migration_manager_->GetDetectedValues() &
      CreditCardSaveManager::DetectedValue::HAS_GOOGLE_PAYMENTS_ACCOUNT);
}

TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_ShouldAddMigrateCardsBillableServiceNumberInRequest) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_TRUE(local_card_migration_manager_->LocalCardMigrationWasTriggered());

  // Confirm that the preflight request contained
  // kMigrateCardsBillableServiceNumber in the request.
  EXPECT_EQ(payments::kMigrateCardsBillableServiceNumber,
            payments_client_->billable_service_number_in_request());
}

TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_ShouldAddUploadCardSourceInRequest_CheckoutFlow) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_TRUE(local_card_migration_manager_->LocalCardMigrationWasTriggered());

  // Confirm that the preflight request contained the correct UploadCardSource.
  EXPECT_EQ(payments::PaymentsClient::UploadCardSource::
                LOCAL_CARD_MIGRATION_CHECKOUT_FLOW,
            payments_client_->upload_card_source_in_request());
}

TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_ShouldAddUploadCardSourceInRequest_SettingsPage) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card. One migratable credit card will still trigger
  // migration on settings page.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");

  base::HistogramTester histogram_tester;
  // Do the same operation as we bridge back from the settings page.
  local_card_migration_manager_->GetMigratableCreditCards();
  local_card_migration_manager_->AttemptToOfferLocalCardMigration(true);

  EXPECT_FALSE(local_card_migration_manager_->IntermediatePromptWasShown());
  EXPECT_TRUE(local_card_migration_manager_->MainPromptWasShown());

  // Confirm that the preflight request contained the correct UploadCardSource.
  EXPECT_EQ(payments::PaymentsClient::UploadCardSource::
                LOCAL_CARD_MIGRATION_SETTINGS_PAGE,
            payments_client_->upload_card_source_in_request());
}

// Verify that when triggering from settings page, intermediate prompt will not
// be triggered.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_TriggerFromSettingsPage) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card. One migratable credit card will still trigger
  // migration on settings page.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");

  base::HistogramTester histogram_tester;
  // Do the same operation as we bridge back from the settings page.
  local_card_migration_manager_->GetMigratableCreditCards();
  local_card_migration_manager_->AttemptToOfferLocalCardMigration(true);

  EXPECT_FALSE(local_card_migration_manager_->IntermediatePromptWasShown());
  EXPECT_TRUE(local_card_migration_manager_->MainPromptWasShown());

  // Verify that metrics are correctly logged to the SettingsPage sub-histogram.
  // Triggering from settings page won't show intermediate bubble.
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.SettingsPage",
      AutofillMetrics::INTERMEDIATE_BUBBLE_SHOWN, 0);
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.SettingsPage",
      AutofillMetrics::INTERMEDIATE_BUBBLE_ACCEPTED, 0);
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.SettingsPage",
      AutofillMetrics::MAIN_DIALOG_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.LocalCardMigrationOrigin.SettingsPage",
      AutofillMetrics::MAIN_DIALOG_ACCEPTED, 1);
}

// Verify that when triggering from submitted form, intermediate prompt and main
// prompt are both triggered.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_TriggerFromSubmittedForm) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card, so it will trigger migration.
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);
  EXPECT_TRUE(local_card_migration_manager_->IntermediatePromptWasShown());
  EXPECT_TRUE(local_card_migration_manager_->MainPromptWasShown());
}

// Verify that when triggering from submitted form, intermediate prompt and main
// prompt are not triggered as user previously rejected the prompt.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_DontTriggerFromSubmittedForm) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);

  // Set that previously user rejected this prompt.
  prefs::SetLocalCardMigrationPromptPreviouslyCancelled(
      autofill_client_.GetPrefs(), true);

  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card, so it will trigger migration.
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  // Migration should not be offered because user previously rejected
  // migration prompt.
  FormSubmitted(credit_card_form);
  EXPECT_FALSE(local_card_migration_manager_->IntermediatePromptWasShown());
  EXPECT_FALSE(local_card_migration_manager_->MainPromptWasShown());
}

// Verify that given the parsed response from the payments client, the migration
// status is correctly set.
TEST_F(LocalCardMigrationManagerTest, MigrateCreditCard_MigrationSuccess) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card for migration.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");

  // Verify that it exists in the local database.
  EXPECT_TRUE(personal_data_.GetCreditCardByNumber("4111111111111111"));

  // Get the migratable credit cards.
  local_card_migration_manager_->GetMigratableCreditCards();

  // Set the parsed response to success.
  SetUpMigrationResponseForGuid(
      local_card_migration_manager_->migratable_credit_cards_[0]
          .credit_card()
          .guid(),
      autofill::kMigrationResultSuccess);

  EXPECT_EQ(local_card_migration_manager_->migratable_credit_cards_[0]
                .migration_status(),
            autofill::MigratableCreditCard::MigrationStatus::UNKNOWN);

  local_card_migration_manager_->AttemptToOfferLocalCardMigration(true);

  EXPECT_EQ(local_card_migration_manager_->migratable_credit_cards_[0]
                .migration_status(),
            autofill::MigratableCreditCard::MigrationStatus::SUCCESS_ON_UPLOAD);

  // Local card should *not* be present as it is migrated already.
  EXPECT_FALSE(personal_data_.GetCreditCardByNumber("4111111111111111"));
}

// Verify that given the parsed response from the payments client, the migration
// status is correctly set.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_MigrationTemporaryFailure) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card. One migratable credit card will still trigger
  // migration on settings page.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");

  // Verify that it exists in local database.
  EXPECT_TRUE(personal_data_.GetCreditCardByNumber("4111111111111111"));

  // Get the migratable credit cards.
  local_card_migration_manager_->GetMigratableCreditCards();

  // Set the parsed response to temporary failure.
  SetUpMigrationResponseForGuid(
      local_card_migration_manager_->migratable_credit_cards_[0]
          .credit_card()
          .guid(),
      autofill::kMigrationResultTemporaryFailure);

  EXPECT_EQ(local_card_migration_manager_->migratable_credit_cards_[0]
                .migration_status(),
            autofill::MigratableCreditCard::MigrationStatus::UNKNOWN);

  // Start the migration.
  local_card_migration_manager_->AttemptToOfferLocalCardMigration(true);

  EXPECT_EQ(local_card_migration_manager_->migratable_credit_cards_[0]
                .migration_status(),
            autofill::MigratableCreditCard::MigrationStatus::FAILURE_ON_UPLOAD);

  // Local card should be present as it is not migrated.
  EXPECT_TRUE(personal_data_.GetCreditCardByNumber("4111111111111111"));
}

// Verify that given the parsed response from the payments client, the migration
// status is correctly set.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_MigrationPermanentFailure) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card. One migratable credit card will still trigger
  // migration on settings page.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");

  // Verify that it exists in local database.
  EXPECT_TRUE(personal_data_.GetCreditCardByNumber("4111111111111111"));

  // Get the migratable credit cards.
  local_card_migration_manager_->GetMigratableCreditCards();

  // Set the parsed response to permanent failure.
  SetUpMigrationResponseForGuid(
      local_card_migration_manager_->migratable_credit_cards_[0]
          .credit_card()
          .guid(),
      autofill::kMigrationResultPermanentFailure);

  EXPECT_EQ(local_card_migration_manager_->migratable_credit_cards_[0]
                .migration_status(),
            autofill::MigratableCreditCard::MigrationStatus::UNKNOWN);

  // Start the migration.
  local_card_migration_manager_->AttemptToOfferLocalCardMigration(true);

  EXPECT_EQ(local_card_migration_manager_->migratable_credit_cards_[0]
                .migration_status(),
            autofill::MigratableCreditCard::MigrationStatus::FAILURE_ON_UPLOAD);

  // Local card should be present as it is not migrated.
  EXPECT_TRUE(personal_data_.GetCreditCardByNumber("4111111111111111"));
}

// Verify selected cards are correctly passed to manager.
TEST_F(LocalCardMigrationManagerTest, MigrateCreditCard_ToggleIsChosen) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  AddLocalCreditCard(personal_data_, "Flo Master", "5454545454545454", "11",
                     test::NextYear().c_str(), "1", "guid2");
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  local_card_migration_manager_->GetMigratableCreditCards();

  autofill_client_.set_migration_card_selections(
      std::vector<std::string>{"guid1"});
  local_card_migration_manager_->AttemptToOfferLocalCardMigration(true);

  EXPECT_EQ(static_cast<int>(
                local_card_migration_manager_->migratable_credit_cards_.size()),
            1);
  EXPECT_EQ(local_card_migration_manager_->migratable_credit_cards_[0]
                .credit_card()
                .guid(),
            "guid1");
}

TEST_F(LocalCardMigrationManagerTest, DeleteLocalCardViaMigrationDialog) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillCreditCardLocalCardMigration);

  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");

  EXPECT_TRUE(personal_data_.GetCreditCardWithGUID("guid1"));

  local_card_migration_manager_->OnUserDeletedLocalCardViaMigrationDialog(
      "guid1");

  EXPECT_FALSE(personal_data_.GetCreditCardWithGUID("guid1"));
}

// Use one local card with more valid local cards available, don't show prompt
// if max strikes reached.
TEST_F(LocalCardMigrationManagerTest,
       MigrateLocalCreditCard_MaxStrikesReached) {
  scoped_feature_list_.InitWithFeatures(
      // Enabled
      {features::kAutofillCreditCardLocalCardMigration,
       features::kAutofillLocalCardMigrationUsesStrikeSystemV2},
      // Disabled
      {});

  LocalCardMigrationStrikeDatabase local_card_migration_strike_database =
      LocalCardMigrationStrikeDatabase(strike_database_);
  local_card_migration_strike_database.AddStrikes(7);

  EXPECT_EQ(local_card_migration_strike_database.GetStrikes(), 7);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  // Add a local credit card whose |TypeAndLastFourDigits| matches what we will
  // enter below.
  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  // Add another local credit card, so it will trigger migration.
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid2");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  base::HistogramTester histogram_tester;
  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);

  // Local card migration not triggered since max strikes have been reached.
  EXPECT_FALSE(local_card_migration_manager_->LocalCardMigrationWasTriggered());

  // Verify that the correct histogram entry was logged.
  histogram_tester.ExpectBucketCount(
      "Autofill.StrikeDatabase.LocalCardMigrationNotOfferedDueToMaxStrikes",
      AutofillMetrics::SaveTypeMetric::LOCAL, 1);
}

// Use one server card with more valid local cards available, don't show prompt
// if max strikes reached.
TEST_F(LocalCardMigrationManagerTest,
       MigrateServerCreditCard_MaxStrikesReached) {
  scoped_feature_list_.InitWithFeatures(
      // Enabled
      {features::kAutofillCreditCardLocalCardMigration,
       features::kAutofillLocalCardMigrationUsesStrikeSystemV2},
      // Disabled
      {});

  LocalCardMigrationStrikeDatabase local_card_migration_strike_database =
      LocalCardMigrationStrikeDatabase(strike_database_);
  local_card_migration_strike_database.AddStrikes(7);

  EXPECT_EQ(local_card_migration_strike_database.GetStrikes(), 7);

  // Set the billing_customer_number Priority Preference to designate
  // existence of a Payments account.
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);

  // Add a masked server credit card whose |TypeAndLastFourDigits| matches what
  // we will enter below.
  CreditCard credit_card(CreditCard::MASKED_SERVER_CARD, "a123");
  test::SetCreditCardInfo(&credit_card, "Flo Master", "1111", "11",
                          test::NextYear().c_str(), "1");
  credit_card.SetNetworkForMaskedCard(kVisaCard);
  personal_data_.AddServerCreditCard(credit_card);
  // Add one valid local credit card, so it will trigger migration
  AddLocalCreditCard(personal_data_, "Flo Master", "5555555555554444", "11",
                     test::NextYear().c_str(), "1", "guid1");

  // Set up our credit card form data.
  FormData credit_card_form;
  test::CreateTestCreditCardFormData(&credit_card_form, true, false);
  FormsSeen(std::vector<FormData>(1, credit_card_form));

  base::HistogramTester histogram_tester;
  // Edit the data, and submit.
  EditCreditCardFrom(credit_card_form, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "123");
  FormSubmitted(credit_card_form);

  // Local card migration not triggered since max strikes have been reached.
  EXPECT_FALSE(local_card_migration_manager_->LocalCardMigrationWasTriggered());

  // Verify that the correct histogram entry was logged.
  histogram_tester.ExpectBucketCount(
      "Autofill.StrikeDatabase.LocalCardMigrationNotOfferedDueToMaxStrikes",
      AutofillMetrics::SaveTypeMetric::SERVER, 1);
}

// When local card migration is attempted and some cards aren't selected,
// 3 strikes should be added.
TEST_F(LocalCardMigrationManagerTest,
       MigrateCreditCard_StrikesAddedWhenSomeCardsNotSelected) {
  scoped_feature_list_.InitWithFeatures(
      // Enabled
      {features::kAutofillCreditCardLocalCardMigration,
       features::kAutofillLocalCardMigrationUsesStrikeSystemV2},
      // Disabled
      {});

  LocalCardMigrationStrikeDatabase local_card_migration_strike_database =
      LocalCardMigrationStrikeDatabase(strike_database_);
  // LocalCardMigrationStrikeDatabase should initially have no strikes.
  EXPECT_EQ(local_card_migration_strike_database.GetStrikes(), 0);

  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  AddLocalCreditCard(personal_data_, "Flo Master", "5454545454545454", "11",
                     test::NextYear().c_str(), "1", "guid2");
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  local_card_migration_manager_->GetMigratableCreditCards();

  // Only select one of the two cards.
  autofill_client_.set_migration_card_selections(
      std::vector<std::string>{"guid1"});
  local_card_migration_manager_->AttemptToOfferLocalCardMigration(true);

  EXPECT_EQ(local_card_migration_strike_database.GetStrikes(), 3);
}

// When local card migration is accepted, UMA metrics for LocalCardMigration
// strike count is logged.
TEST_F(LocalCardMigrationManagerTest, MigrateCreditCard_StrikeCountUMALogged) {
  scoped_feature_list_.InitWithFeatures(
      // Enabled
      {features::kAutofillCreditCardLocalCardMigration,
       features::kAutofillLocalCardMigrationUsesStrikeSystemV2},
      // Disabled
      {});

  AddLocalCreditCard(personal_data_, "Flo Master", "4111111111111111", "11",
                     test::NextYear().c_str(), "1", "guid1");
  AddLocalCreditCard(personal_data_, "Flo Master", "5454545454545454", "11",
                     test::NextYear().c_str(), "1", "guid2");
  autofill_client_.GetPrefs()->SetDouble(prefs::kAutofillBillingCustomerNumber,
                                         12345);
  local_card_migration_manager_->GetMigratableCreditCards();

  // Add 4 LocalCardMigration strikes.
  LocalCardMigrationStrikeDatabase local_card_migration_strike_database =
      LocalCardMigrationStrikeDatabase(strike_database_);
  local_card_migration_strike_database.AddStrikes(4);
  EXPECT_EQ(local_card_migration_strike_database.GetStrikes(), 4);

  base::HistogramTester histogram_tester;

  // Select the cards.
  autofill_client_.set_migration_card_selections(
      std::vector<std::string>{"guid1", "guid2"});
  local_card_migration_manager_->AttemptToOfferLocalCardMigration(true);

  // Verify that the strike count was logged when card migration accepted.
  histogram_tester.ExpectBucketCount(
      "Autofill.StrikeDatabase.StrikesPresentWhenLocalCardMigrationAccepted", 4,
      1);
}

}  // namespace autofill
