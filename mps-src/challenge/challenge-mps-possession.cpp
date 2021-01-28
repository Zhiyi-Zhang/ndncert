/*
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

#include "challenge-mps-possession.hpp"
#include <ndn-cxx/security/verification-helpers.hpp>
#include <ndn-cxx/security/signing-helpers.hpp>
#include <ndn-cxx/security/transform/public-key.hpp>
#include <ndn-cxx/util/io.hpp>
#include <ndn-cxx/util/random.hpp>

namespace ndn {
namespace ndncert {

NDN_LOG_INIT(ndncert.challenge.mps.possession);
NDNCERT_REGISTER_CHALLENGE(ChallengeMpsPossession, "MpsPossession");

const std::string ChallengeMpsPossession::PARAMETER_KEY_CREDENTIAL_CERT = "issued-cert";
const std::string ChallengeMpsPossession::PARAMETER_KEY_NONCE = "nonce";
const std::string ChallengeMpsPossession::PARAMETER_KEY_PROOF = "proof";
const std::string ChallengeMpsPossession::NEED_PROOF = "need-proof";

ChallengeMpsPossession::ChallengeMpsPossession(const std::string& configPath)
    : ChallengeModule("MpsPossession", 1, time::seconds(60))
{
  if (configPath.empty()) {
    m_configFile = std::string(NDNCERT_SYSCONFDIR) + "/ndncert/challenge-credential.conf";
  }
  else {
    m_configFile = configPath;
  }
}

void
ChallengeMpsPossession::parseConfigFile()
{
  JsonSection config;
  try {
    boost::property_tree::read_json(m_configFile, config);
  }
  catch (const boost::property_tree::info_parser_error& error) {
    NDN_THROW(std::runtime_error("Failed to parse configuration file " + m_configFile +
                                             " " + error.message() + " line " + std::to_string(error.line())));
  }

  if (config.begin() == config.end()) {
    NDN_THROW(std::runtime_error("Error processing configuration file: " + m_configFile + " no data"));
  }

  m_trustAnchors.clear();
  auto anchorList = config.get_child("anchor-list");
  auto it = anchorList.begin();
  for (; it != anchorList.end(); it++) {
    std::istringstream ss(it->second.get("certificate", ""));
    auto cert = io::load<security::Certificate>(ss);
    if (cert == nullptr) {
      NDN_LOG_ERROR("Cannot load the certificate from config file");
      continue;
    }
    m_trustAnchors.push_back(*cert);
  }
}

// For CA
std::tuple<ErrorCode, std::string>
ChallengeMpsPossession::handleChallengeRequest(const Block& params, ca::RequestState& request)
{
  params.parse();
  if (m_trustAnchors.empty()) {
    parseConfigFile();
  }
  security::Certificate credential;
  const uint8_t* signature = nullptr;
  size_t signatureLen = 0;
  const auto& elements = params.elements();
  for (size_t i = 0; i < elements.size() - 1; i++) {
    if (elements[i].type() == tlv::ParameterKey && elements[i + 1].type() == tlv::ParameterValue) {
      if (readString(elements[i]) == PARAMETER_KEY_CREDENTIAL_CERT) {
        try {
          credential.wireDecode(elements[i + 1].blockFromValue());
        }
        catch (const std::exception& e) {
          NDN_LOG_ERROR("Cannot load challenge parameter: credential " << e.what());
          return returnWithError(request, ErrorCode::INVALID_PARAMETER, "Cannot challenge credential: credential." + std::string(e.what()));
        }
      }
      else if (readString(elements[i]) == PARAMETER_KEY_PROOF) {
        signature = elements[i + 1].value();
        signatureLen = elements[i + 1].value_size();
      }
    }
  }

  // verify the credential and the self-signed cert
  if (request.status == Status::BEFORE_CHALLENGE) {
    NDN_LOG_TRACE("Challenge Interest arrives. Check certificate and init the challenge");
    // check the certificate
    bool checkOK = false;
    if (credential.hasContent() && signatureLen == 0) {
      Name signingKeyName = credential.getSignatureInfo().getKeyLocator().getName();
      security::transform::PublicKey key;
      const auto &pubKeyBuffer = credential.getPublicKey();
      key.loadPkcs8(pubKeyBuffer.data(), pubKeyBuffer.size());
      for (auto anchor : m_trustAnchors) {
        if (anchor.getKeyName() == signingKeyName) {
          if (security::verifySignature(credential, anchor)) {
            checkOK = true;
          }
        }
      }
    } else {
        return returnWithError(request, ErrorCode::BAD_INTEREST_FORMAT, "Cannot find certificate");
    }
    if (!checkOK) {
      return returnWithError(request, ErrorCode::INVALID_PARAMETER, "Certificate cannot be verified");
    }

    // for the first time, init the challenge
    std::array<uint8_t, 16> secretCode{};
    random::generateSecureBytes(secretCode.data(), 16);
    JsonSection secretJson;
    secretJson.add(PARAMETER_KEY_NONCE, toHex(secretCode.data(), 16));
    auto credential_block = credential.wireEncode();
    secretJson.add(PARAMETER_KEY_CREDENTIAL_CERT, toHex(credential_block.wire(), credential_block.size()));
    NDN_LOG_TRACE("Secret for request " << toHex(request.requestId.data(), request.requestId.size())
                  << " : " << toHex(secretCode.data(), 16));
    return returnWithNewChallengeStatus(request, NEED_PROOF, std::move(secretJson), m_maxAttemptTimes, m_secretLifetime);
  } else if (request.challengeState && request.challengeState->challengeStatus == NEED_PROOF) {
    NDN_LOG_TRACE("Challenge Interest (proof) arrives. Check the proof");
    //check the format and load credential
    if (credential.hasContent() || signatureLen == 0) {
        return returnWithError(request, ErrorCode::BAD_INTEREST_FORMAT, "Cannot find certificate");
    }
    credential = security::Certificate(Block(fromHex(request.challengeState->secrets.get(PARAMETER_KEY_CREDENTIAL_CERT, ""))));
    auto secretCode = *fromHex(request.challengeState->secrets.get(PARAMETER_KEY_NONCE, ""));

    //check the proof
    security::transform::PublicKey key;
    const auto& pubKeyBuffer = credential.getPublicKey();
    key.loadPkcs8(pubKeyBuffer.data(), pubKeyBuffer.size());
    if (security::verifySignature(secretCode.data(), secretCode.size(), signature, signatureLen, key)) {
      return returnWithSuccess(request);
    }
    return returnWithError(request, ErrorCode::INVALID_PARAMETER,
            "Cannot verify the proof of private key against credential.");
  }
  NDN_LOG_TRACE("Proof of possession: bad state");
  return returnWithError(request, ErrorCode::INVALID_PARAMETER, "Fail to recognize the request.");
}

// For Client
std::multimap<std::string, std::string>
ChallengeMpsPossession::getRequestedParameterList(Status status, const std::string& challengeStatus)
{
  std::multimap<std::string, std::string> result;
  if (status == Status::BEFORE_CHALLENGE) {
    result.emplace(PARAMETER_KEY_CREDENTIAL_CERT, "Please provide the certificate issued by a trusted CA.");
    return result;
  } else if (status == Status::CHALLENGE && challengeStatus == NEED_PROOF) {
    result.emplace(PARAMETER_KEY_PROOF, "Please sign a Data packet with request ID as the content.");
  } else {
    NDN_THROW(std::runtime_error("Unexpected status or challenge status."));
  }

  return result;
}

Block
ChallengeMpsPossession::genChallengeRequestTLV(Status status, const std::string& challengeStatus,
                                            const std::multimap<std::string, std::string>& params)
{
  Block request(tlv::EncryptedPayload);
  if (status == Status::BEFORE_CHALLENGE) {
    if (params.size() != 1) {
      NDN_THROW(std::runtime_error("Wrong parameter provided."));
    }
    request.push_back(makeStringBlock(tlv::SelectedChallenge, CHALLENGE_TYPE));
    for (const auto& item : params) {
      if (std::get<0>(item) == PARAMETER_KEY_CREDENTIAL_CERT) {
        request.push_back(makeStringBlock(tlv::ParameterKey, PARAMETER_KEY_CREDENTIAL_CERT));
        Block valueBlock(tlv::ParameterValue);
        auto& certTlvStr = std::get<1>(item);
        valueBlock.push_back(Block((uint8_t*)certTlvStr.c_str(), certTlvStr.size()));
        request.push_back(valueBlock);
      }
      else {
        NDN_THROW(std::runtime_error("Wrong parameter provided."));
      }
    }
  } else if (status == Status::CHALLENGE && challengeStatus == NEED_PROOF){
    if (params.size() != 1) {
      NDN_THROW(std::runtime_error("Wrong parameter provided."));
    }
    for (const auto &item : params) {
      if (std::get<0>(item) == PARAMETER_KEY_PROOF) {
        request.push_back(makeStringBlock(tlv::ParameterKey, PARAMETER_KEY_PROOF));
        auto &sigTlvStr = std::get<1>(item);
        Block valueBlock = makeBinaryBlock(tlv::ParameterValue, (uint8_t *) sigTlvStr.c_str(),
                                           sigTlvStr.size());
        request.push_back(valueBlock);
      } else {
        NDN_THROW(std::runtime_error("Wrong parameter provided."));
      }
    }
  } else {
    NDN_THROW(std::runtime_error("Unexpected status or challenge status."));
  }
  request.encode();
  return request;
}

void
ChallengeMpsPossession::fulfillParameters(std::multimap<std::string, std::string>& params,
                                       KeyChain& keyChain, const Name& issuedCertName,
                                       const std::array<uint8_t, 16>& nonce)
{
  auto& pib = keyChain.getPib();
  auto id = pib.getIdentity(security::extractIdentityFromCertName(issuedCertName));
  auto issuedCert = id.getKey(security::extractKeyNameFromCertName(issuedCertName)).getCertificate(issuedCertName);
  auto issuedCertTlv = issuedCert.wireEncode();
  auto signatureTlv = keyChain.sign(nonce.data(), nonce.size(), security::signingByCertificate(issuedCertName));
  for (auto& item : params) {
    if (std::get<0>(item) == PARAMETER_KEY_CREDENTIAL_CERT) {
      std::get<1>(item) = std::string((char*)issuedCertTlv.wire(), issuedCertTlv.size());
    }
    else if (std::get<0>(item) == PARAMETER_KEY_PROOF) {
      std::get<1>(item) = std::string((char*)signatureTlv.value(), signatureTlv.value_size());
    }
  }
  return;
}

} // namespace ndncert
} // namespace ndn
