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

#include "detail/request-encoder.hpp"
#include <ndn-cxx/security/transform/base64-encode.hpp>
#include <ndn-cxx/security/transform/buffer-source.hpp>
#include <ndn-cxx/security/transform/stream-sink.hpp>

namespace ndn {
namespace ndncert {

Block
requesttlv::encodeApplicationParameters(RequestType requestType, const std::vector <uint8_t>& ecdhPub,
                                        const security::Certificate& certRequest)
{
  Block
  request(ndn::tlv::ApplicationParameters);
  request.push_back(makeBinaryBlock(tlv::EcdhPub, ecdhPub.data(), ecdhPub.size()));
  if (requestType == RequestType::NEW || requestType == RequestType::RENEW) {
    request.push_back(makeNestedBlock(tlv::CertRequest, certRequest));
  }
  else if (requestType == RequestType::REVOKE) {
    request.push_back(makeNestedBlock(tlv::CertToRevoke, certRequest));
  }
  request.encode();
  return request;
}

void
requesttlv::decodeApplicationParameters(const Block& payload, RequestType requestType,
                                        std::vector <uint8_t>& ecdhPub,
                                        shared_ptr <security::Certificate>& clientCert)
{
  payload.parse();

  const auto& ecdhBlock = payload.get(tlv::EcdhPub);
  ecdhPub.resize(ecdhBlock.value_size());
  std::memcpy(ecdhPub.data(), ecdhBlock.value(), ecdhBlock.value_size());

  Block requestPayload;
  if (requestType == RequestType::NEW) {
    requestPayload = payload.get(tlv::CertRequest);
  }
  else if (requestType == RequestType::REVOKE) {
    requestPayload = payload.get(tlv::CertToRevoke);
  }
  requestPayload.parse();

  security::Certificate cert = security::Certificate(requestPayload.get(ndn::tlv::Data));
  clientCert = std::make_shared<security::Certificate>(cert);
}

Block
requesttlv::encodeDataContent(const std::vector <uint8_t>& ecdhKey, const std::array<uint8_t, 32>& salt,
                              const RequestId& requestId,
                              const std::vector <std::string>& challenges)
{
  Block response(ndn::tlv::Content);
  response.push_back(makeBinaryBlock(tlv::EcdhPub, ecdhKey.data(), ecdhKey.size()));
  response.push_back(makeBinaryBlock(tlv::Salt, salt.data(), salt.size()));
  response.push_back(makeBinaryBlock(tlv::RequestId, requestId.data(), requestId.size()));
  for (const auto& entry: challenges) {
    response.push_back(makeStringBlock(tlv::Challenge, entry));
  }
  response.encode();
  return response;
}

std::list <std::string>
requesttlv::decodeDataContent(const Block& content, std::vector <uint8_t>& ecdhKey,
                              std::array<uint8_t, 32>& salt, RequestId& requestId)
{
  content.parse();

  const auto& ecdhBlock = content.get(tlv::EcdhPub);
  ecdhKey.resize(ecdhBlock.value_size());
  std::memcpy(ecdhKey.data(), ecdhBlock.value(), ecdhBlock.value_size());

  const auto& saltBlock = content.get(tlv::Salt);
  std::memcpy(salt.data(), saltBlock.value(), saltBlock.value_size());

  const auto& requestIdBlock = content.get(tlv::RequestId);
  std::memcpy(requestId.data(), requestIdBlock.value(), requestIdBlock.value_size());

  std::list <std::string> challenges;
  for (auto const& element : content.elements()) {
    if (element.type() == tlv::Challenge) {
      challenges.push_back(readString(element));
    }
  }
  return challenges;
}

} // namespace ndncert
} // namespace ndn