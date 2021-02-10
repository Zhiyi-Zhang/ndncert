/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2017-2020, Regents of the University of California.
 *
 * This file is part of ndncert, a certificate management system based on NDN.
 *
 * ndncert is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * ndncert is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received copies of the GNU General Public License along with
 * ndncert, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ndncert authors and contributors.
 */

#include "challenge/challenge-possession.hpp"
#include "test-common.hpp"
#include "detail/challenge-encoder.hpp"

namespace ndn {
namespace ndncert {
namespace tests {

BOOST_FIXTURE_TEST_SUITE(TestChallengeCredential, IdentityManagementFixture)

BOOST_AUTO_TEST_CASE(LoadConfig)
{
  ChallengePossession challenge("./tests/unit-tests/config-files/config-challenge-possession");
  BOOST_CHECK_EQUAL(challenge.CHALLENGE_TYPE, "Possession");

  challenge.parseConfigFile();
  BOOST_CHECK_EQUAL(challenge.m_trustAnchors.size(), 1);
  auto cert = challenge.m_trustAnchors.front();
  BOOST_CHECK_EQUAL(cert.getName(),
                    "/ndn/site1/KEY/%11%BC%22%F4c%15%FF%17/self/%FD%00%00%01Y%C8%14%D9%A5");
}

BOOST_AUTO_TEST_CASE(HandleChallengeRequest)
{
  // create trust anchor
  ChallengePossession challenge("./tests/unit-tests/config-files/config-challenge-possession");
  auto identity = addIdentity(Name("/trust"));
  auto key = identity.getDefaultKey();
  auto trustAnchor = key.getDefaultCertificate();
  challenge.parseConfigFile();
  challenge.m_trustAnchors.front() = trustAnchor;

  // create certificate request
  auto identityA = addIdentity(Name("/example"));
  auto keyA = identityA.getDefaultKey();
  auto certA = key.getDefaultCertificate();
  RequestId requestId = {{101}};
  ca::RequestState state;
  state.caPrefix = Name("/example");
  state.requestId = requestId;
  state.requestType = RequestType::NEW;
  state.cert = certA;

  // create requester's credential
  auto identityB = addIdentity(Name("/trust/cert"));
  auto keyB = identityB.getDefaultKey();
  auto credentialName = Name(keyB.getName()).append("Credential").appendVersion();
  security::Certificate credential;
  credential.setName(credentialName);
  credential.setContent(keyB.getPublicKey().data(), keyB.getPublicKey().size());
  SignatureInfo signatureInfo;
  signatureInfo.setValidityPeriod(security::ValidityPeriod(time::system_clock::now(), time::system_clock::now() +
                                  time::minutes(1)));
  m_keyChain.sign(credential, signingByCertificate(trustAnchor).setSignatureInfo(signatureInfo));
  m_keyChain.addCertificate(keyB, credential);

  // using private key to sign cert request
  auto params = challenge.getRequestedParameterList(state.status, "");
  ChallengePossession::fulfillParameters(params, m_keyChain, credential.getName(), std::array<uint8_t, 16>{});
  Block paramsTlv = challenge.genChallengeRequestTLV(state.status, "", params);
  challenge.handleChallengeRequest(paramsTlv, state);
  BOOST_CHECK_EQUAL(statusToString(state.status), statusToString(Status::CHALLENGE));
  BOOST_CHECK_EQUAL(state.challengeState->challengeStatus, "need-proof");

  // reply from server
  auto nonceBuf = fromHex(state.challengeState->secrets.get("nonce", ""));
  std::array<uint8_t, 16> nonce{};
  memcpy(nonce.data(), nonceBuf->data(), 16);
  auto params2 = challenge.getRequestedParameterList(state.status, state.challengeState->challengeStatus);
  ChallengePossession::fulfillParameters(params2, m_keyChain, credential.getName(), nonce);
  Block paramsTlv2 = challenge.genChallengeRequestTLV(state.status, state.challengeState->challengeStatus, params2);
  challenge.handleChallengeRequest(paramsTlv2, state);
  BOOST_CHECK_EQUAL(statusToString(state.status), statusToString(Status::PENDING));
}

BOOST_AUTO_TEST_CASE(HandleChallengeRequestProofFail)
{
  // create trust anchor
  ChallengePossession challenge("./tests/unit-tests/config-files/config-challenge-possession");
  auto identity = addIdentity(Name("/trust"));
  auto key = identity.getDefaultKey();
  auto trustAnchor = key.getDefaultCertificate();
  challenge.parseConfigFile();
  challenge.m_trustAnchors.front() = trustAnchor;

  // create certificate request
  auto identityA = addIdentity(Name("/example"));
  auto keyA = identityA.getDefaultKey();
  auto certA = key.getDefaultCertificate();
  RequestId requestId = {{101}};
  ca::RequestState state;
  state.caPrefix = Name("/example");
  state.requestId = requestId;
  state.requestType = RequestType::NEW;
  state.cert = certA;

  // create requester's credential
  auto identityB = addIdentity(Name("/trust/cert"));
  auto keyB = identityB.getDefaultKey();
  auto credentialName = Name(keyB.getName()).append("Credential").appendVersion();
  security::Certificate credential;
  credential.setName(credentialName);
  credential.setContent(keyB.getPublicKey().data(), keyB.getPublicKey().size());
  SignatureInfo signatureInfo;
  signatureInfo.setValidityPeriod(security::ValidityPeriod(time::system_clock::now(), time::system_clock::now() +
                                                                                        time::minutes(1)));
  m_keyChain.sign(credential, signingByCertificate(trustAnchor).setSignatureInfo(signatureInfo));
  m_keyChain.addCertificate(keyB, credential);

  // using private key to sign cert request
  auto params = challenge.getRequestedParameterList(state.status, "");
  ChallengePossession::fulfillParameters(params, m_keyChain, credential.getName(), std::array<uint8_t, 16>{});
  Block paramsTlv = challenge.genChallengeRequestTLV(state.status, "", params);
  challenge.handleChallengeRequest(paramsTlv, state);
  BOOST_CHECK_EQUAL(statusToString(state.status), statusToString(Status::CHALLENGE));
  BOOST_CHECK_EQUAL(state.challengeState->challengeStatus, "need-proof");

  // reply from server
  std::array<uint8_t, 16> nonce{};
  auto params2 = challenge.getRequestedParameterList(state.status, state.challengeState->challengeStatus);
  ChallengePossession::fulfillParameters(params2, m_keyChain, credential.getName(), nonce);
  Block paramsTlv2 = challenge.genChallengeRequestTLV(state.status, state.challengeState->challengeStatus, params2);
  challenge.handleChallengeRequest(paramsTlv2, state);
  BOOST_CHECK_EQUAL(statusToString(state.status), statusToString(Status::FAILURE));
}

#ifdef NDNCERT_HAS_NDNMPS
BOOST_AUTO_TEST_CASE(HandleChallengeBlsRequest)
{
  // create trust anchor
  ChallengePossession challenge("./tests/unit-tests/config-files/config-challenge-possession");
  auto identity = addIdentity(Name("/trust"));
  auto key = identity.getDefaultKey();
  auto trustAnchor = key.getDefaultCertificate();
  challenge.parseConfigFile();
  challenge.m_trustAnchors.front() = trustAnchor;

  // create certificate request
  auto identityA = addIdentity(Name("/example"));
  auto keyA = identityA.getDefaultKey();
  auto certA = key.getDefaultCertificate();
  RequestId requestId = {{101}};
  ca::RequestState state;
  state.caPrefix = Name("/example");
  state.requestId = requestId;
  state.requestType = RequestType::NEW;
  state.cert = certA;

  // create requester's credential
  MpsSigner keyB("/trust/cert/KEY/123");
  auto credentialName = Name(keyB.getSignerKeyName()).append("Credential").appendVersion();
  security::Certificate credential;
  credential.setName(credentialName);
  {
    auto keyBuf = keyB.getpublicKeyStr();
    credential.setContent(keyBuf.data(), keyBuf.size());
  }
  SignatureInfo signatureInfo;
  signatureInfo.setValidityPeriod(security::ValidityPeriod(time::system_clock::now(), time::system_clock::now() +
                                                                                      time::minutes(1)));
  m_keyChain.sign(credential, signingByCertificate(trustAnchor).setSignatureInfo(signatureInfo));

  // using private key to sign cert request
  auto params = challenge.getRequestedParameterList(state.status, "");
  ChallengePossession::fulfillParameters(params, credential, keyB, std::array<uint8_t, 16>{});
  Block paramsTlv = challenge.genChallengeRequestTLV(state.status, "", params);
  challenge.handleChallengeRequest(paramsTlv, state);
  BOOST_CHECK_EQUAL(statusToString(state.status), statusToString(Status::CHALLENGE));
  BOOST_CHECK_EQUAL(state.challengeState->challengeStatus, "need-proof");

  // reply from server
  auto nonceBuf = fromHex(state.challengeState->secrets.get("nonce", ""));
  std::array<uint8_t, 16> nonce{};
  memcpy(nonce.data(), nonceBuf->data(), 16);
  auto params2 = challenge.getRequestedParameterList(state.status, state.challengeState->challengeStatus);
  ChallengePossession::fulfillParameters(params2, credential, keyB, nonce);
  Block paramsTlv2 = challenge.genChallengeRequestTLV(state.status, state.challengeState->challengeStatus, params2);
  challenge.handleChallengeRequest(paramsTlv2, state);
  BOOST_CHECK_EQUAL(statusToString(state.status), statusToString(Status::PENDING));
}

BOOST_AUTO_TEST_CASE(HandleChallengeBlsProofFail)
{
  // create trust anchor
  ChallengePossession challenge("./tests/unit-tests/config-files/config-challenge-possession");
  auto identity = addIdentity(Name("/trust"));
  auto key = identity.getDefaultKey();
  auto trustAnchor = key.getDefaultCertificate();
  challenge.parseConfigFile();
  challenge.m_trustAnchors.front() = trustAnchor;

  // create certificate request
  auto identityA = addIdentity(Name("/example"));
  auto keyA = identityA.getDefaultKey();
  auto certA = key.getDefaultCertificate();
  RequestId requestId = {{101}};
  ca::RequestState state;
  state.caPrefix = Name("/example");
  state.requestId = requestId;
  state.requestType = RequestType::NEW;
  state.cert = certA;

  // create requester's credential
  MpsSigner keyB("/trust/cert/KEY/123");
  auto credentialName = Name(keyB.getSignerKeyName()).append("Credential").appendVersion();
  security::Certificate credential;
  credential.setName(credentialName);
  {
    auto keyBuf = keyB.getpublicKeyStr();
    credential.setContent(keyBuf.data(), keyBuf.size());
  }
  SignatureInfo signatureInfo;
  signatureInfo.setValidityPeriod(security::ValidityPeriod(time::system_clock::now(), time::system_clock::now() +
                                                                                      time::minutes(1)));
  m_keyChain.sign(credential, signingByCertificate(trustAnchor).setSignatureInfo(signatureInfo));

  // using private key to sign cert request
  auto params = challenge.getRequestedParameterList(state.status, "");
  ChallengePossession::fulfillParameters(params, credential, keyB, std::array<uint8_t, 16>{});
  Block paramsTlv = challenge.genChallengeRequestTLV(state.status, "", params);
  challenge.handleChallengeRequest(paramsTlv, state);
  BOOST_CHECK_EQUAL(statusToString(state.status), statusToString(Status::CHALLENGE));
  BOOST_CHECK_EQUAL(state.challengeState->challengeStatus, "need-proof");

  // reply from server
  auto nonceBuf = fromHex(state.challengeState->secrets.get("nonce", ""));
  std::array<uint8_t, 16> nonce{};
  memcpy(nonce.data(), nonceBuf->data(), 16);
  nonce[1] = 123; // Induced error
  auto params2 = challenge.getRequestedParameterList(state.status, state.challengeState->challengeStatus);
  ChallengePossession::fulfillParameters(params2, credential, keyB, nonce);
  Block paramsTlv2 = challenge.genChallengeRequestTLV(state.status, state.challengeState->challengeStatus, params2);
  challenge.handleChallengeRequest(paramsTlv2, state);
  BOOST_CHECK_EQUAL(statusToString(state.status), statusToString(Status::FAILURE));
}

#endif

BOOST_AUTO_TEST_SUITE_END()

} // namespace tests
} // namespace ndncert
} // namespace ndn
