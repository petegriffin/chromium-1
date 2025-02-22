/**
 * Copyright 2014 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * The one and only peer connection in this page.
 * @private
 */
var gPeerConnection = null;

/**
 * This stores ICE candidates generated on this side.
 * @private
 */
var gIceCandidates = [];

/**
 * This stores last ICE gathering state emitted on this side.
 * @private
 */
var gIceGatheringState = 'no-gathering-state';

/**
 * Keeps track of whether we have seen crypto information in the SDP.
 * @private
 */
var gHasSeenCryptoInSdp = 'no-crypto-seen';

/**
 * The default audio codec that should be used when creating an offer.
 * @private
 */
var gDefaultAudioCodec = null;

/**
 * The default video codec that should be used when creating an offer.
 * @private
 */
var gDefaultVideoCodec = null;

/**
 * The default video codec profile that should be used when creating an offer.
 * @private
 */
var gDefaultVideoCodecProfile = null;

/**
 * The default video target bitrate that should be used when creating an offer.
 * @private
 */
var gDefaultVideoTargetBitrate = null;

/**
 * Flag to indicate if HW or SW video codec is preferred.
 * @private
 */
var gDefaultPreferHwVideoCodec = null;

/**
 * Flag to indicate if Opus Dtx should be enabled.
 * @private
 */
var gOpusDtx = false;

/** @private */
var gNegotiationNeededCount = 0;

/** @private */
var gTrackEvents = [];

// Public interface to tests. These are expected to be called with
// ExecuteJavascript invocations from the browser tests and will return answers
// through the DOM automation controller.

/**
 * Creates a peer connection. Must be called before most other public functions
 * in this file. Alternatively, see |preparePeerConnectionWithCertificate|.
 * @param {Object} keygenAlgorithm Unless null, this is an |AlgorithmIdentifier|
 * to be used as parameter to |RTCPeerConnection.generateCertificate|. The
 * resulting certificate will be used by the peer connection. If null, a default
 * certificate is generated by the |RTCPeerConnection| instead.
 * @param {string} peerConnectionConstraints Unless null, this adds peer
 * connection constraints when creating new RTCPeerConnection.
 */
function preparePeerConnection(
    keygenAlgorithm = null, peerConnectionConstraints = null) {
  if (gPeerConnection !== null)
    throw failTest('Creating peer connection, but we already have one.');

  if (keygenAlgorithm === null) {
    gPeerConnection = createPeerConnection_(null, peerConnectionConstraints);
    returnToTest('ok-peerconnection-created');
  } else {
    RTCPeerConnection.generateCertificate(keygenAlgorithm).then(
        function(certificate) {
          preparePeerConnectionWithCertificate(certificate,
              peerConnectionConstraints);
        },
        function() {
          failTest('Certificate generation failed. keygenAlgorithm: ' +
              JSON.stringify(keygenAlgorithm));
        });
  }
}

/**
 * Creates a peer connection. Must be called before most other public functions
 * in this file. Alternatively, see |preparePeerConnection|.
 * @param {!Object} certificate The |RTCCertificate| that will be used by the
 * peer connection.
 * @param {string} peerConnectionConstraints Unless null, this adds peer
 * connection constraints when creating new RTCPeerConnection.
 */
function preparePeerConnectionWithCertificate(
    certificate, peerConnectionConstraints = null) {
  if (gPeerConnection !== null)
    throw failTest('Creating peer connection, but we already have one.');
  gPeerConnection = createPeerConnection_(
      {iceServers:[], certificates:[certificate]}, peerConnectionConstraints);
  returnToTest('ok-peerconnection-created');
}

/**
 * Sets the flag to force Opus Dtx to be used when creating an offer.
 */
function forceOpusDtx() {
  gOpusDtx = true;
  returnToTest('ok-forced');
}

/**
 * Sets the default audio codec to be used when creating an offer and returns
 * "ok" to test.
 * @param {string} audioCodec promotes the specified codec to be the default
 *     audio codec, e.g. the first one in the list on the 'm=audio' SDP offer
 *     line. |audioCodec| is the case-sensitive codec name, e.g. 'opus' or
 *     'ISAC'.
 */
function setDefaultAudioCodec(audioCodec) {
  gDefaultAudioCodec = audioCodec;
  returnToTest('ok');
}

/**
 * Sets the default video codec to be used when creating an offer and returns
 * "ok" to test.
 * @param {string} videoCodec promotes the specified codec to be the default
 *     video codec, e.g. the first one in the list on the 'm=video' SDP offer
 *     line. |videoCodec| is the case-sensitive codec name, e.g. 'VP8' or
 *     'H264'.
 * @param {string} profile promotes the specified codec profile.
 * @param {bool} preferHwVideoCodec specifies what codec to use from the
 *     'm=video' line when there are multiple codecs with the name |videoCodec|.
 *     If true, it will return the last codec with that name, and if false, it
 *     will return the first codec with that name.
 */
function setDefaultVideoCodec(videoCodec, preferHwVideoCodec, profile) {
  gDefaultVideoCodec = videoCodec;
  gDefaultPreferHwVideoCodec = preferHwVideoCodec;
  gDefaultVideoCodecProfile = profile;
  returnToTest('ok');
}

/**
 * Sets the default video target bitrate to be used when creating an offer and
 * returns "ok" to test.
 * @param {int} modifies "b=AS:" line with the given value.
 */
function setDefaultVideoTargetBitrate(bitrate) {
  gDefaultVideoTargetBitrate = bitrate;
  returnToTest('ok');
}

/**
 * Creates a data channel with the specified label.
 * Returns 'ok-created' to test.
 */
function createDataChannel(label) {
  peerConnection_().createDataChannel(label);
  returnToTest('ok-created');
}

/**
 * Asks this page to create a local offer.
 *
 * Returns a string on the format ok-(JSON encoded session description).
 *
 * @param {!Object} constraints Any createOffer constraints.
 */
function createLocalOffer(constraints) {
  peerConnection_().createOffer(
      function(localOffer) {
        success('createOffer');

        setLocalDescription(peerConnection, localOffer);
        if (gDefaultAudioCodec !== null) {
          localOffer.sdp = setSdpDefaultAudioCodec(localOffer.sdp,
                                                   gDefaultAudioCodec);
        }
        if (gDefaultVideoCodec !== null) {
          localOffer.sdp = setSdpDefaultVideoCodec(
              localOffer.sdp, gDefaultVideoCodec, gDefaultPreferHwVideoCodec,
              gDefaultVideoCodecProfile);
        }
        if (gOpusDtx) {
          localOffer.sdp = setOpusDtxEnabled(localOffer.sdp);
        }
        if (gDefaultVideoTargetBitrate !== null) {
          localOffer.sdp = setSdpVideoTargetBitrate(localOffer.sdp,
                                                    gDefaultVideoTargetBitrate);
        }
        returnToTest('ok-' + JSON.stringify(localOffer));
      },
      function(error) { failure('createOffer', error); },
      constraints);
}

/**
 * Asks this page to accept an offer and generate an answer.
 *
 * Returns a string on the format ok-(JSON encoded session description).
 *
 * @param {!string} sessionDescJson A JSON-encoded session description of type
 *     'offer'.
 * @param {!Object} constraints Any createAnswer constraints.
 */
function receiveOfferFromPeer(sessionDescJson, constraints) {
  offer = parseJson_(sessionDescJson);
  if (!offer.type)
    failTest('Got invalid session description from peer: ' + sessionDescJson);
  if (offer.type != 'offer')
    failTest('Expected to receive offer from peer, got ' + offer.type);

  var sessionDescription = new RTCSessionDescription(offer);
  peerConnection_().setRemoteDescription(
      sessionDescription,
      function() { success('setRemoteDescription'); },
      function(error) { failure('setRemoteDescription', error); });

  peerConnection_().createAnswer(
      function(answer) {
        success('createAnswer');
        setLocalDescription(peerConnection, answer);
        if (gOpusDtx) {
          answer.sdp = setOpusDtxEnabled(answer.sdp);
        }
        if (gDefaultVideoTargetBitrate !== null) {
          answer.sdp = setSdpVideoTargetBitrate(answer.sdp,
                                                gDefaultVideoTargetBitrate);
        }
        returnToTest('ok-' + JSON.stringify(answer));
      },
      function(error) { failure('createAnswer', error); },
      constraints);
}

/**
 * Verifies that the codec previously set using setDefault[Audio/Video]Codec()
 * is the default audio/video codec, e.g. the first one in the list on the
 * 'm=audio'/'m=video' SDP answer line. If this is not the case, |failure|
 * occurs. If no codec was previously set using setDefault[Audio/Video]Codec(),
 * this function will return 'ok-no-defaults-set'.
 *
 * @param {!string} sessionDescJson A JSON-encoded session description.
 */
function verifyDefaultCodecs(sessionDescJson) {
  let sessionDesc = parseJson_(sessionDescJson);
  if (!sessionDesc.type) {
    failure('verifyDefaultCodecs',
             'Invalid session description: ' + sessionDescJson);
  }
  if (gDefaultAudioCodec !== null && gDefaultVideoCodec !== null) {
    returnToTest('ok-no-defaults-set');
    return;
  }
  if (gDefaultAudioCodec !== null) {
    let defaultAudioCodec = getSdpDefaultAudioCodec(sessionDesc.sdp);
    if (defaultAudioCodec === null) {
      failure('verifyDefaultCodecs',
               'Could not determine default audio codec.');
    }
    if (gDefaultAudioCodec !== defaultAudioCodec) {
      failure('verifyDefaultCodecs',
               'Expected default audio codec ' + gDefaultAudioCodec +
               ', got ' + defaultAudioCodec + '.');
    }
  }
  if (gDefaultVideoCodec !== null) {
    let defaultVideoCodec = getSdpDefaultVideoCodec(sessionDesc.sdp);
    if (defaultVideoCodec === null) {
      failure('verifyDefaultCodecs',
               'Could not determine default video codec.');
    }
    if (gDefaultVideoCodec !== defaultVideoCodec) {
      failure('verifyDefaultCodecs',
               'Expected default video codec ' + gDefaultVideoCodec +
               ', got ' + defaultVideoCodec + '.');
    }
  }
  returnToTest('ok-verified');
}

/**
 * Verifies that the peer connection's local description contains one of
 * |certificate|'s fingerprints.
 *
 * Returns 'ok-verified' on success.
 */
function verifyLocalDescriptionContainsCertificate(certificate) {
  let localDescription = peerConnection_().localDescription;
  if (localDescription == null)
    throw failTest('localDescription is null.');
  for (let i = 0; i < certificate.getFingerprints().length; ++i) {
    let fingerprintSdp = 'a=fingerprint:' +
        certificate.getFingerprints()[i].algorithm + ' ' +
        certificate.getFingerprints()[i].value.toUpperCase();
    if (localDescription.sdp.includes(fingerprintSdp)) {
      returnToTest('ok-verified');
      return;
    }
  }
  if (!localDescription.sdp.includes('a=fingerprint'))
    throw failTest('localDescription does not contain any fingerprints.');
  throw failTest('Certificate fingerprint not found in localDescription.');
}

/**
 * Asks this page to accept an answer generated by the peer in response to a
 * previous offer by this page
 *
 * Returns a string ok-accepted-answer on success.
 *
 * @param {!string} sessionDescJson A JSON-encoded session description of type
 *     'answer'.
 */
function receiveAnswerFromPeer(sessionDescJson) {
  answer = parseJson_(sessionDescJson);
  if (!answer.type)
    failTest('Got invalid session description from peer: ' + sessionDescJson);
  if (answer.type != 'answer')
    failTest('Expected to receive answer from peer, got ' + answer.type);

  var sessionDescription = new RTCSessionDescription(answer);
  peerConnection_().setRemoteDescription(
      sessionDescription,
      function() {
        success('setRemoteDescription');
        returnToTest('ok-accepted-answer');
      },
      function(error) { failure('setRemoteDescription', error); });
}

/**
 * Adds the local stream to the peer connection. You will have to re-negotiate
 * the call for this to take effect in the call.
 */
function addLocalStream() {
  addLocalStreamToPeerConnection(peerConnection_());
  returnToTest('ok-added');
}

/**
 * Loads a file with WebAudio and connects it to the peer connection.
 *
 * The loadAudioAndAddToPeerConnection will return ok-added to the test when
 * the sound is loaded and added to the peer connection. The sound will start
 * playing when you call playAudioFile.
 *
 * @param url URL pointing to the file to play. You can assume that you can
 *     serve files from the repository's file system. For instance, to serve a
 *     file from chrome/test/data/pyauto_private/webrtc/file.wav, pass in a path
 *     relative to this directory (e.g. ../pyauto_private/webrtc/file.wav).
 */
function addAudioFile(url) {
  loadAudioAndAddToPeerConnection(url, peerConnection_());
}

/**
 * Must be called after addAudioFile.
 */
function playAudioFile() {
  playPreviouslyLoadedAudioFile(peerConnection_());
  returnToTest('ok-playing');
}

/**
 * Hangs up a started call. Returns ok-call-hung-up on success.
 */
function hangUp() {
  peerConnection_().close();
  gPeerConnection = null;
  returnToTest('ok-call-hung-up');
}

/**
 * Retrieves all ICE candidates generated on this side. Must be called after
 * ICE candidate generation is triggered (for instance by running a call
 * negotiation). This function will wait if necessary if we're not done
 * generating ICE candidates on this side.
 *
 * Returns a JSON-encoded array of RTCIceCandidate instances to the test.
 */
function getAllIceCandidates() {
  if (peerConnection_().iceGatheringState != 'complete') {
    console.log('Still ICE gathering - waiting...');
    setTimeout(getAllIceCandidates, 100);
    return;
  }

  returnToTest(JSON.stringify(gIceCandidates));
}

/**
 * Receives ICE candidates from the peer.
 *
 * Returns ok-received-candidates to the test on success.
 *
 * @param iceCandidatesJson a JSON-encoded array of RTCIceCandidate instances.
 */
function receiveIceCandidates(iceCandidatesJson) {
  var iceCandidates = parseJson_(iceCandidatesJson);
  if (!iceCandidates.length)
    throw failTest('Received invalid ICE candidate list from peer: ' +
        iceCandidatesJson);

  iceCandidates.forEach(function(iceCandidate) {
    if (!iceCandidate.candidate)
      failTest('Received invalid ICE candidate from peer: ' +
          iceCandidatesJson);

    peerConnection_().addIceCandidate(new RTCIceCandidate(iceCandidate,
        function() { success('addIceCandidate'); },
        function(error) { failure('addIceCandidate', error); }
    ));
  });

  returnToTest('ok-received-candidates');
}

/**
 * Sets the mute state of the selected media element.
 *
 * Returns ok-muted on success.
 *
 * @param elementId The id of the element to mute.
 * @param muted The mute state to set.
 */
function setMediaElementMuted(elementId, muted) {
  var element = document.getElementById(elementId);
  if (!element)
    throw failTest('Cannot mute ' + elementId + '; does not exist.');
  element.muted = muted;
  returnToTest('ok-muted');
}

/**
 * Returns
 */
function hasSeenCryptoInSdp() {
  returnToTest(gHasSeenCryptoInSdp);
}

/**
 * Verifies that the legacy |RTCPeerConnection.getStats| returns stats, that
 * each stats member is a string, and that each stats member is on the
 * whitelist.
 *
 * Returns ok-got-stats on success.
 */
function verifyLegacyStatsGenerated() {
  peerConnection_().getStats(
    function(response) {
      var reports = response.result();
      var numStats = 0;
      for (var i = 0; i < reports.length; i++) {
        var statNames = reports[i].names();
        numStats += statNames.length;
        for (var j = 0; j < statNames.length; j++) {
          var statValue = reports[i].stat(statNames[j]);
          if (typeof statValue != 'string')
            throw failTest('A stat was returned that is not a string.');
          if (!isWhitelistedLegacyStat(statNames[j])) {
            throw failTest(
                '"' + statNames[j] + '" is not a whitelisted stat. Exposing ' +
                'new metrics in the legacy getStats() API is not allowed. ' +
                'Please follow the standardization process: ' +
                'https://docs.google.com/document/d/1q1CJVUqJ6YW9NNRc0tENkLNn' +
                'y8AHrKZfqjy3SL89zjc/edit?usp=sharing');
          }
        }
      }
      if (numStats === 0)
        throw failTest('No stats was returned by getStats.');
      returnToTest('ok-got-stats');
    });
}

/**
 * Measures the performance of the legacy (callback-based)
 * |RTCPeerConnection.getStats| and returns the time it took in milliseconds as
 * a double (DOMHighResTimeStamp, accurate to one thousandth of a millisecond).
 *
 * Returns "ok-" followed by a double.
 */
function measureGetStatsCallbackPerformance() {
  let t0 = performance.now();
  peerConnection_().getStats(
    function(response) {
      let t1 = performance.now();
      returnToTest('ok-' + (t1 - t0));
    });
}

/**
 * Returns the last iceGatheringState emitted from icegatheringstatechange.
 */
function getLastGatheringState() {
  returnToTest(gIceGatheringState);
}

/**
 * Returns "ok-negotiation-count-is-" followed by the number of times
 * onnegotiationneeded has fired. This will include any currently queued
 * negotiationneeded events.
 */
function getNegotiationNeededCount() {
  window.setTimeout(function() {
    returnToTest('ok-negotiation-count-is-' + gNegotiationNeededCount);
  }, 0);
}

/**
 * Gets the track and stream IDs of each "ontrack" event that has been fired on
 * the peer connection in chronological order.
 *
 * Returns "ok-" followed by a series of space-separated
 * "RTCTrackEvent <track id> <stream ids>".
 */
function getTrackEvents() {
  let result = '';
  gTrackEvents.forEach(function(event) {
    if (event.receiver.track != event.track)
      throw failTest('RTCTrackEvent\'s track does not match its receiver\'s.');
    let eventString = 'RTCTrackEvent ' + event.track.id;
    event.streams.forEach(function(stream) {
      eventString += ' ' + stream.id;
    });
    if (result.length)
      result += ' ';
    result += eventString;
  });
  returnToTest('ok-' + result);
}

// Internals.

/** @private */
function createPeerConnection_(rtcConfig, peerConnectionConstraints) {
  try {
    peerConnection =
        new RTCPeerConnection(rtcConfig, peerConnectionConstraints);
  } catch (exception) {
    throw failTest('Failed to create peer connection: ' + exception);
  }
  peerConnection.onaddstream = addStreamCallback_;
  peerConnection.onremovestream = removeStreamCallback_;
  peerConnection.onicecandidate = iceCallback_;
  peerConnection.onicegatheringstatechange = iceGatheringCallback_;
  peerConnection.onnegotiationneeded = negotiationNeededCallback_;
  peerConnection.ontrack = onTrackCallback_;
  return peerConnection;
}

/** @private */
function peerConnection_() {
  if (gPeerConnection == null)
    throw failTest('Trying to use peer connection, but none was created.');
  return gPeerConnection;
}

/** @private */
function iceCallback_(event) {
  if (event.candidate)
    gIceCandidates.push(event.candidate);
}

/** @private */
function iceGatheringCallback_() {
  gIceGatheringState = peerConnection.iceGatheringState;
}

/** @private */
function negotiationNeededCallback_() {
  ++gNegotiationNeededCount;
}

/** @private */
function onTrackCallback_(event) {
  gTrackEvents.push(event);
}

/** @private */
function setLocalDescription(peerConnection, sessionDescription) {
  if (sessionDescription.sdp.search('a=crypto') != -1 ||
      sessionDescription.sdp.search('a=fingerprint') != -1)
    gHasSeenCryptoInSdp = 'crypto-seen';

  peerConnection.setLocalDescription(
    sessionDescription,
    function() { success('setLocalDescription'); },
    function(error) { failure('setLocalDescription', error); });
}

/** @private */
function addStreamCallback_(event) {
  debug('Receiving remote stream...');
  var videoTag = document.getElementById('remote-view');
  videoTag.srcObject = event.stream;
}

/** @private */
function removeStreamCallback_(event) {
  debug('Call ended.');
  document.getElementById('remote-view').src = '';
}

/**
 * Parses JSON-encoded session descriptions and ICE candidates.
 * @private
 */
function parseJson_(json) {
  // Escape since the \r\n in the SDP tend to get unescaped.
  jsonWithEscapedLineBreaks = json.replace(/\r\n/g, '\\r\\n');
  try {
    return JSON.parse(jsonWithEscapedLineBreaks);
  } catch (exception) {
    failTest('Failed to parse JSON: ' + jsonWithEscapedLineBreaks + ', got ' +
             exception);
  }
}

/**
 * The legacy stats API is non-standard. It should be deprecated and removed.
 * New stats are not allowed. To add new metrics, follow the standardization
 * process, and only add it to the promise-based getStats() API. See:
 * https://docs.google.com/document/d/1q1CJVUqJ6YW9NNRc0tENkLNny8AHrKZfqjy3SL89zjc/edit?usp=sharing
 * @private
 */
function isWhitelistedLegacyStat(stat) {
  const whitelist = new Set([
      "aecDivergentFilterFraction",
      "audioOutputLevel",
      "audioInputLevel",
      "bytesSent",
      "concealedSamples",
      "concealmentEvents",
      "packetsSent",
      "bytesReceived",
      "label",
      "packetsReceived",
      "packetsLost",
      "protocol",
      "totalSamplesReceived",
      "transportId",
      "selectedCandidatePairId",
      "ssrc",
      "state",
      "datachannelid",
      "framesDecoded",
      "framesEncoded",
      "jitterBufferDelay",
      "codecImplementationName",
      "mediaType",
      "qpSum",
      "googAccelerateRate",
      "googActiveConnection",
      "googActualEncBitrate",
      "googAvailableReceiveBandwidth",
      "googAvailableSendBandwidth",
      "googAvgEncodeMs",
      "googBucketDelay",
      "googBandwidthLimitedResolution",
      "requestsSent",
      "consentRequestsSent",
      "responsesSent",
      "requestsReceived",
      "responsesReceived",
      "stunKeepaliveRequestsSent",
      "stunKeepaliveResponsesReceived",
      "stunKeepaliveRttTotal",
      "stunKeepaliveRttSquaredTotal",
      "ipAddress",
      "networkType",
      "portNumber",
      "priority",
      "transport",
      "candidateType",
      "googChannelId",
      "googCodecName",
      "googComponent",
      "googContentName",
      "googContentType",
      "googCpuLimitedResolution",
      "googDecodingCTSG",
      "googDecodingCTN",
      "googDecodingMuted",
      "googDecodingNormal",
      "googDecodingPLC",
      "googDecodingCNG",
      "googDecodingPLCCNG",
      "googDerBase64",
      "dtlsCipher",
      "googEchoCancellationEchoDelayMedian",
      "googEchoCancellationEchoDelayStdDev",
      "googEchoCancellationReturnLoss",
      "googEchoCancellationReturnLossEnhancement",
      "googEncodeUsagePercent",
      "googExpandRate",
      "googFingerprint",
      "googFingerprintAlgorithm",
      "googFirsReceived",
      "googFirsSent",
      "googFrameHeightInput",
      "googFrameHeightReceived",
      "googFrameHeightSent",
      "googFrameRateReceived",
      "googFrameRateDecoded",
      "googFrameRateOutput",
      "googDecodeMs",
      "googMaxDecodeMs",
      "googCurrentDelayMs",
      "googTargetDelayMs",
      "googJitterBufferMs",
      "googMinPlayoutDelayMs",
      "googRenderDelayMs",
      "googCaptureStartNtpTimeMs",
      "googFrameRateInput",
      "googFrameRateSent",
      "googFrameWidthInput",
      "googFrameWidthReceived",
      "googFrameWidthSent",
      "googHasEnteredLowResolution",
      "hugeFramesSent",
      "googInitiator",
      "googInterframeDelayMax",
      "googIssuerId",
      "googJitterReceived",
      "googLocalAddress",
      "localCandidateId",
      "googLocalCandidateType",
      "localCertificateId",
      "googAdaptationChanges",
      "googNacksReceived",
      "googNacksSent",
      "googPreemptiveExpandRate",
      "googPlisReceived",
      "googPlisSent",
      "googPreferredJitterBufferMs",
      "googReadable",
      "googRemoteAddress",
      "remoteCandidateId",
      "googRemoteCandidateType",
      "remoteCertificateId",
      "googResidualEchoLikelihood",
      "googResidualEchoLikelihoodRecentMax",
      "googAnaBitrateActionCounter",
      "googAnaChannelActionCounter",
      "googAnaDtxActionCounter",
      "googAnaFecActionCounter",
      "googAnaFrameLengthIncreaseCounter",
      "googAnaFrameLengthDecreaseCounter",
      "googAnaUplinkPacketLossFraction",
      "googRetransmitBitrate",
      "googRtt",
      "googSecondaryDecodedRate",
      "googSecondaryDiscardedRate",
      "packetsDiscardedOnSend",
      "googSpeechExpandRate",
      "srtpCipher",
      "googTargetEncBitrate",
      "totalAudioEnergy",
      "totalSamplesDuration",
      "googTransmitBitrate",
      "googTransportType",
      "googTrackId",
      "googTimingFrameInfo",
      "googTypingNoiseState",
      "googWritable" ]);
  return whitelist.has(stat);
}
