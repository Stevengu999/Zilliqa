/*
 * Copyright (c) 2018 Zilliqa
 * This source code is being disclosed to you solely for the purpose of your
 * participation in testing Zilliqa. You may view, compile and run the code for
 * that purpose and pursuant to the protocols and algorithms that are programmed
 * into, and intended by, the code. You may not do anything else with the code
 * without express permission from Zilliqa Research Pte. Ltd., including
 * modifying or publishing the code (or any part of it), and developing or
 * forming another public or private blockchain network. This source code is
 * provided 'as is' and no warranties are given as to title or non-infringement,
 * merchantability or fitness for purpose and, to the extent permitted by law,
 * all liability for your use of the code is disclaimed. Some programs in this
 * code are governed by the GNU General Public License v3.0 (available at
 * https://www.gnu.org/licenses/gpl-3.0.en.html) ('GPLv3'). The programs that
 * are governed by GPLv3.0 are those programs that are located in the folders
 * src/depends and tests/depends and which include a reference to GPLv3 in their
 * program files.
 */

#include "MicroBlockHeader.h"
#include "libMessage/Messenger.h"
#include "libUtils/Logger.h"

using namespace std;
using namespace boost::multiprecision;

MicroBlockHeader::MicroBlockHeader() { m_blockNum = (uint64_t)-1; }

MicroBlockHeader::MicroBlockHeader(const vector<unsigned char>& src,
                                   unsigned int offset) {
  if (!Deserialize(src, offset)) {
    LOG_GENERAL(WARNING, "We failed to init MicroBlockHeader.");
  }
}

MicroBlockHeader::MicroBlockHeader(
    uint8_t type, uint32_t version, uint32_t shardId, const uint256_t& gasLimit,
    const uint256_t& gasUsed, const BlockHash& prevHash,
    const uint64_t& blockNum, const uint256_t& timestamp,
    const TxnHash& txRootHash, uint32_t numTxs, const PubKey& minerPubKey,
    const uint64_t& dsBlockNum, const BlockHash& dsBlockHeader,
    const StateHash& stateDeltaHash, const TxnHash& tranReceiptHash,
    const CommitteeHash& committeeHash)
    : BlockHeaderBase(committeeHash),
      m_type(type),
      m_version(version),
      m_shardId(shardId),
      m_gasLimit(gasLimit),
      m_gasUsed(gasUsed),
      m_prevHash(prevHash),
      m_blockNum(blockNum),
      m_timestamp(timestamp),
      m_hash{txRootHash, stateDeltaHash, tranReceiptHash},
      m_numTxs(numTxs),
      m_minerPubKey(minerPubKey),
      m_dsBlockNum(dsBlockNum),
      m_dsBlockHeader(dsBlockHeader) {}

bool MicroBlockHeader::Serialize(vector<unsigned char>& dst,
                                 unsigned int offset) const {
  if (!Messenger::SetMicroBlockHeader(dst, offset, *this)) {
    LOG_GENERAL(WARNING, "Messenger::SetMicroBlockHeader failed.");
    return false;
  }

  return true;
}

bool MicroBlockHeader::Deserialize(const vector<unsigned char>& src,
                                   unsigned int offset) {
  if (!Messenger::GetMicroBlockHeader(src, offset, *this)) {
    LOG_GENERAL(WARNING, "Messenger::GetMicroBlockHeader failed.");
    return false;
  }

  return true;
}

const uint8_t& MicroBlockHeader::GetType() const { return m_type; }

const uint32_t& MicroBlockHeader::GetVersion() const { return m_version; }

const uint32_t& MicroBlockHeader::GetShardId() const { return m_shardId; }

const uint256_t& MicroBlockHeader::GetGasLimit() const { return m_gasLimit; }

const uint256_t& MicroBlockHeader::GetGasUsed() const { return m_gasUsed; }

const BlockHash& MicroBlockHeader::GetPrevHash() const { return m_prevHash; }

const uint64_t& MicroBlockHeader::GetBlockNum() const { return m_blockNum; }

const uint256_t& MicroBlockHeader::GetTimestamp() const { return m_timestamp; }

const uint32_t& MicroBlockHeader::GetNumTxs() const { return m_numTxs; }

const PubKey& MicroBlockHeader::GetMinerPubKey() const { return m_minerPubKey; }

const uint64_t& MicroBlockHeader::GetDSBlockNum() const { return m_dsBlockNum; }

const BlockHash& MicroBlockHeader::GetDSBlockHeader() const {
  return m_dsBlockHeader;
}

const TxnHash& MicroBlockHeader::GetTxRootHash() const {
  return m_hash.m_txRootHash;
}

const StateHash& MicroBlockHeader::GetStateDeltaHash() const {
  return m_hash.m_stateDeltaHash;
}

const TxnHash& MicroBlockHeader::GetTranReceiptHash() const {
  return m_hash.m_tranReceiptHash;
}

const MicroBlockHashSet& MicroBlockHeader::GetHash() const { return m_hash; }

bool MicroBlockHeader::operator==(const MicroBlockHeader& header) const {
  return std::tie(m_type, m_version, m_shardId, m_gasLimit, m_gasUsed,
                  m_prevHash, m_blockNum, m_timestamp, m_hash, m_numTxs,
                  m_minerPubKey, m_dsBlockNum, m_dsBlockHeader) ==
         std::tie(header.m_type, header.m_version, header.m_shardId,
                  header.m_gasLimit, header.m_gasUsed, header.m_prevHash,
                  header.m_blockNum, header.m_timestamp, header.m_hash,
                  header.m_numTxs, header.m_minerPubKey, header.m_dsBlockNum,
                  header.m_dsBlockHeader);
}

bool MicroBlockHeader::operator<(const MicroBlockHeader& header) const {
  return std::tie(header.m_type, header.m_version, header.m_shardId,
                  header.m_gasLimit, header.m_gasUsed, header.m_prevHash,
                  header.m_blockNum, header.m_timestamp, header.m_hash,
                  header.m_numTxs, header.m_minerPubKey, header.m_dsBlockNum,
                  header.m_dsBlockHeader) >
         std::tie(m_type, m_version, m_shardId, m_gasLimit, m_gasUsed,
                  m_prevHash, m_blockNum, m_timestamp, m_hash, m_numTxs,
                  m_minerPubKey, m_dsBlockNum, m_dsBlockHeader);
}

bool MicroBlockHeader::operator>(const MicroBlockHeader& header) const {
  return header < *this;
}
