// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/autofill/local_card_migration_dialog_controller_impl.h"

#include <stddef.h>

#include "base/bind.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/autofill/strike_database_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/autofill/autofill_ui_util.h"
#include "chrome/browser/ui/autofill/local_card_migration_dialog.h"
#include "chrome/browser/ui/autofill/local_card_migration_dialog_factory.h"
#include "chrome/browser/ui/autofill/local_card_migration_dialog_state.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "components/autofill/core/browser/autofill_metrics.h"
#include "components/autofill/core/browser/payments/local_card_migration_manager.h"
#include "components/autofill/core/browser/payments/local_card_migration_strike_database.h"
#include "components/autofill/core/browser/payments/payments_service_url.h"
#include "components/autofill/core/browser/payments/strike_database.h"
#include "components/autofill/core/browser/validation.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"

namespace autofill {

LocalCardMigrationDialogControllerImpl::LocalCardMigrationDialogControllerImpl(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      pref_service_(
          user_prefs::UserPrefs::Get(web_contents->GetBrowserContext())) {}

LocalCardMigrationDialogControllerImpl::
    ~LocalCardMigrationDialogControllerImpl() {
  if (local_card_migration_dialog_)
    local_card_migration_dialog_->CloseDialog();
}

void LocalCardMigrationDialogControllerImpl::ShowOfferDialog(
    std::unique_ptr<base::DictionaryValue> legal_message,
    const std::string& user_email,
    const std::vector<MigratableCreditCard>& migratable_credit_cards,
    AutofillClient::LocalCardMigrationCallback start_migrating_cards_callback) {
  if (local_card_migration_dialog_)
    local_card_migration_dialog_->CloseDialog();

  if (!LegalMessageLine::Parse(*legal_message, &legal_message_lines_,
                               /*escape_apostrophes=*/true)) {
    AutofillMetrics::LogLocalCardMigrationDialogOfferMetric(
        AutofillMetrics::
            LOCAL_CARD_MIGRATION_DIALOG_NOT_SHOWN_INVALID_LEGAL_MESSAGE);
    return;
  }

  view_state_ = LocalCardMigrationDialogState::kOffered;
  // Need to create the icon first otherwise the dialog will not be shown.
  UpdateLocalCardMigrationIcon(web_contents());
  local_card_migration_dialog_ =
      CreateLocalCardMigrationDialogView(this, web_contents());
  start_migrating_cards_callback_ = std::move(start_migrating_cards_callback);
  migratable_credit_cards_ = migratable_credit_cards;
  user_email_ = user_email;
  local_card_migration_dialog_->ShowDialog();
  UpdateLocalCardMigrationIcon(web_contents());
  dialog_is_visible_duration_timer_ = base::ElapsedTimer();

  AutofillMetrics::LogLocalCardMigrationDialogOfferMetric(
      AutofillMetrics::LOCAL_CARD_MIGRATION_DIALOG_SHOWN);
}

void LocalCardMigrationDialogControllerImpl::UpdateCreditCardIcon(
    const base::string16& tip_message,
    const std::vector<MigratableCreditCard>& migratable_credit_cards,
    AutofillClient::MigrationDeleteCardCallback delete_local_card_callback) {
  if (local_card_migration_dialog_)
    local_card_migration_dialog_->CloseDialog();

  migratable_credit_cards_ = migratable_credit_cards;
  tip_message_ = tip_message;
  delete_local_card_callback_ = delete_local_card_callback;

  view_state_ = LocalCardMigrationDialogState::kFinished;
  for (const auto& cc : migratable_credit_cards) {
    if (cc.migration_status() ==
        MigratableCreditCard::MigrationStatus::FAILURE_ON_UPLOAD) {
      view_state_ = LocalCardMigrationDialogState::kActionRequired;
      break;
    }
  }
  UpdateLocalCardMigrationIcon(web_contents());
}

void LocalCardMigrationDialogControllerImpl::ShowFeedbackDialog() {
  AutofillMetrics::LogLocalCardMigrationDialogOfferMetric(
      AutofillMetrics::LOCAL_CARD_MIGRATION_DIALOG_FEEDBACK_SHOWN);

  local_card_migration_dialog_ =
      CreateLocalCardMigrationDialogView(this, web_contents());
  local_card_migration_dialog_->ShowDialog();
  UpdateLocalCardMigrationIcon(web_contents());
  dialog_is_visible_duration_timer_ = base::ElapsedTimer();
}

void LocalCardMigrationDialogControllerImpl::ShowErrorDialog() {
  AutofillMetrics::LogLocalCardMigrationDialogOfferMetric(
      AutofillMetrics::LOCAL_CARD_MIGRATION_DIALOG_FEEDBACK_SERVER_ERROR_SHOWN);

  local_card_migration_dialog_ =
      CreateLocalCardMigrationErrorDialogView(this, web_contents());
  UpdateLocalCardMigrationIcon(web_contents());
  local_card_migration_dialog_->ShowDialog();
  dialog_is_visible_duration_timer_ = base::ElapsedTimer();
}

void LocalCardMigrationDialogControllerImpl::AddObserver(
    LocalCardMigrationControllerObserver* observer) {
  observer_list_.AddObserver(observer);
}

LocalCardMigrationDialogState
LocalCardMigrationDialogControllerImpl::GetViewState() const {
  return view_state_;
}

const std::vector<MigratableCreditCard>&
LocalCardMigrationDialogControllerImpl::GetCardList() const {
  return migratable_credit_cards_;
}

const LegalMessageLines&
LocalCardMigrationDialogControllerImpl::GetLegalMessageLines() const {
  return legal_message_lines_;
}

const base::string16& LocalCardMigrationDialogControllerImpl::GetTipMessage()
    const {
  return tip_message_;
}

const std::string& LocalCardMigrationDialogControllerImpl::GetUserEmail()
    const {
  return user_email_;
}

void LocalCardMigrationDialogControllerImpl::OnSaveButtonClicked(
    const std::vector<std::string>& selected_cards_guids) {
  AutofillMetrics::LogLocalCardMigrationDialogUserSelectionPercentageMetric(
      selected_cards_guids.size(), migratable_credit_cards_.size());
  AutofillMetrics::LogLocalCardMigrationDialogUserInteractionMetric(
      dialog_is_visible_duration_timer_.Elapsed(),
      AutofillMetrics::LOCAL_CARD_MIGRATION_DIALOG_CLOSED_SAVE_BUTTON_CLICKED);

  std::move(start_migrating_cards_callback_).Run(selected_cards_guids);
  // If flag is disabled, we don't show the credit card icon animation.
  base::FeatureList::IsEnabled(
      features::kAutofillLocalCardMigrationShowFeedback)
      ? NotifyMigrationStarted()
      : NotifyMigrationNoLongerAvailable();
}

void LocalCardMigrationDialogControllerImpl::OnCancelButtonClicked() {
  // Add strikes for local card migration due to user closing the main dialog.
  if (base::FeatureList::IsEnabled(
          features::kAutofillLocalCardMigrationUsesStrikeSystemV2)) {
    LocalCardMigrationStrikeDatabase local_card_migration_strike_database(
        StrikeDatabaseFactory::GetForProfile(
            Profile::FromBrowserContext(web_contents()->GetBrowserContext())));
    local_card_migration_strike_database.AddStrikes(
        LocalCardMigrationStrikeDatabase::kStrikesToAddWhenDialogClosed);
  } else {
    prefs::SetLocalCardMigrationPromptPreviouslyCancelled(pref_service_, true);
  }

  AutofillMetrics::LogLocalCardMigrationDialogUserInteractionMetric(
      dialog_is_visible_duration_timer_.Elapsed(),
      AutofillMetrics::
          LOCAL_CARD_MIGRATION_DIALOG_CLOSED_CANCEL_BUTTON_CLICKED);

  start_migrating_cards_callback_.Reset();
  NotifyMigrationNoLongerAvailable();
}

void LocalCardMigrationDialogControllerImpl::OnDoneButtonClicked() {
  AutofillMetrics::LogLocalCardMigrationDialogUserInteractionMetric(
      dialog_is_visible_duration_timer_.Elapsed(),
      AutofillMetrics::LOCAL_CARD_MIGRATION_DIALOG_CLOSED_DONE_BUTTON_CLICKED);
  NotifyMigrationNoLongerAvailable();
}

void LocalCardMigrationDialogControllerImpl::OnViewCardsButtonClicked() {
  AutofillMetrics::LogLocalCardMigrationDialogUserInteractionMetric(
      dialog_is_visible_duration_timer_.Elapsed(),
      AutofillMetrics::
          LOCAL_CARD_MIGRATION_DIALOG_CLOSED_VIEW_CARDS_BUTTON_CLICKED);

  OpenUrl(payments::GetManageInstrumentsUrl());
  NotifyMigrationNoLongerAvailable();
}

void LocalCardMigrationDialogControllerImpl::OnLegalMessageLinkClicked(
    const GURL& url) {
  OpenUrl(url);
  AutofillMetrics::LogLocalCardMigrationDialogUserInteractionMetric(
      dialog_is_visible_duration_timer_.Elapsed(),
      AutofillMetrics::LOCAL_CARD_MIGRATION_DIALOG_LEGAL_MESSAGE_CLICKED);
}

void LocalCardMigrationDialogControllerImpl::DeleteCard(
    const std::string& deleted_card_guid) {
  DCHECK(delete_local_card_callback_);
  delete_local_card_callback_.Run(deleted_card_guid);

  base::EraseIf(migratable_credit_cards_, [&](const auto& card) {
    return card.credit_card().guid() == deleted_card_guid;
  });

  if (!HasFailedCard()) {
    view_state_ = LocalCardMigrationDialogState::kFinished;
    delete_local_card_callback_.Reset();
  }

  AutofillMetrics::LogLocalCardMigrationDialogUserInteractionMetric(
      dialog_is_visible_duration_timer_.Elapsed(),
      AutofillMetrics::LOCAL_CARD_MIGRATION_DIALOG_DELETE_CARD_ICON_CLICKED);
}

void LocalCardMigrationDialogControllerImpl::OnDialogClosed() {
  if (local_card_migration_dialog_)
    local_card_migration_dialog_ = nullptr;

  UpdateLocalCardMigrationIcon(web_contents());
}

bool LocalCardMigrationDialogControllerImpl::AllCardsInvalid() const {
  // For kOffered state, the migration status of all cards are UNKNOWN,
  // so this function will return true as well. Need an early exit to avoid
  // it.
  if (view_state_ == LocalCardMigrationDialogState::kOffered)
    return false;

  return std::find_if(
             migratable_credit_cards_.begin(), migratable_credit_cards_.end(),
             [](const auto& card) {
               return card.migration_status() ==
                      MigratableCreditCard::MigrationStatus::SUCCESS_ON_UPLOAD;
             }) == migratable_credit_cards_.end();
}

LocalCardMigrationDialog*
LocalCardMigrationDialogControllerImpl::local_card_migration_dialog_view()
    const {
  return local_card_migration_dialog_;
}

void LocalCardMigrationDialogControllerImpl::OpenUrl(const GURL& url) {
  web_contents()->OpenURL(content::OpenURLParams(
      url, content::Referrer(), WindowOpenDisposition::NEW_POPUP,
      ui::PAGE_TRANSITION_LINK, false));
}

bool LocalCardMigrationDialogControllerImpl::HasFailedCard() const {
  return std::find_if(
             migratable_credit_cards_.begin(), migratable_credit_cards_.end(),
             [](const auto& card) {
               return card.migration_status() ==
                      MigratableCreditCard::MigrationStatus::FAILURE_ON_UPLOAD;
             }) != migratable_credit_cards_.end();
}

void LocalCardMigrationDialogControllerImpl::
    NotifyMigrationNoLongerAvailable() {
  for (LocalCardMigrationControllerObserver& observer : observer_list_) {
    observer.OnMigrationNoLongerAvailable();
  }
}

void LocalCardMigrationDialogControllerImpl::NotifyMigrationStarted() {
  for (LocalCardMigrationControllerObserver& observer : observer_list_) {
    observer.OnMigrationStarted();
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(LocalCardMigrationDialogControllerImpl)

}  // namespace autofill
