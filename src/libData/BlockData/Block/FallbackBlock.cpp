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

#include <utility>

#include "FallbackBlock.h"
#include "libMessage/Messenger.h"
#include "libUtils/Logger.h"

using namespace std;
using namespace boost::multiprecision;

FallbackBlock::FallbackBlock() {}

FallbackBlock::FallbackBlock(const vector<unsigned char>& src,
                             unsigned int offset) {
  if (!Deserialize(src, offset)) {
    LOG_GENERAL(WARNING, "We failed to init FallbackBlock");
  }
}

FallbackBlock::FallbackBlock(const FallbackBlockHeader& header,
                             CoSignatures&& cosigs)
    : m_header(header) {
  m_cosigs = move(cosigs);
}

bool FallbackBlock::Serialize(vector<unsigned char>& dst,
                              unsigned int offset) const {
  if (!Messenger::SetFallbackBlock(dst, offset, *this)) {
    LOG_GENERAL(WARNING, "Messenger::SetFallbackBlock failed.");
    return false;
  }

  return true;
}

bool FallbackBlock::Deserialize(const vector<unsigned char>& src,
                                unsigned int offset) {
  if (!Messenger::GetFallbackBlock(src, offset, *this)) {
    LOG_GENERAL(WARNING, "Messenger::GetFallbackBlock failed.");
    return false;
  }

  return true;
}

const FallbackBlockHeader& FallbackBlock::GetHeader() const { return m_header; }

bool FallbackBlock::operator==(const FallbackBlock& block) const {
  return (m_header == block.m_header);
}

bool FallbackBlock::operator<(const FallbackBlock& block) const {
  return m_header < block.m_header;
}

bool FallbackBlock::operator>(const FallbackBlock& block) const {
  return !((*this == block) || (*this < block));
}

const BlockHash& FallbackBlock::GetBlockHash() const { return m_blockHash; }

void FallbackBlock::SetBlockHash(const BlockHash& blockHash) {
  m_blockHash = blockHash;
}