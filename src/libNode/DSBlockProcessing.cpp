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

#include <array>
#include <boost/multiprecision/cpp_int.hpp>
#include <chrono>
#include <functional>
#include <thread>

#include "Node.h"
#include "common/Constants.h"
#include "common/Messages.h"
#include "common/Serializable.h"
#include "depends/common/RLP.h"
#include "depends/libDatabase/MemoryDB.h"
#include "depends/libTrie/TrieDB.h"
#include "depends/libTrie/TrieHash.h"
#include "libConsensus/ConsensusUser.h"
#include "libCrypto/Sha2.h"
#include "libData/AccountData/Account.h"
#include "libData/AccountData/AccountStore.h"
#include "libData/AccountData/Transaction.h"
#include "libMediator/Mediator.h"
#include "libMessage/Messenger.h"
#include "libNetwork/Whitelist.h"
#include "libPOW/pow.h"
#include "libUtils/BitVector.h"
#include "libUtils/DataConversion.h"
#include "libUtils/DetachedFunction.h"
#include "libUtils/HashUtils.h"
#include "libUtils/Logger.h"
#include "libUtils/SanityChecks.h"
#include "libUtils/TimeLockedFunction.h"
#include "libUtils/TimeUtils.h"
#include "libUtils/UpgradeManager.h"

using namespace std;
using namespace boost::multiprecision;

void Node::StoreDSBlockToDisk(const DSBlock& dsblock) {
  LOG_MARKER();

  m_mediator.m_dsBlockChain.AddBlock(dsblock);
  LOG_EPOCH(
      INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
      "Storing DS Block Number: "
          << dsblock.GetHeader().GetBlockNum() << " with Nonce: "
          << ", DS PoW Difficulty: "
          << to_string(dsblock.GetHeader().GetDSDifficulty())
          << ", Difficulty: " << to_string(dsblock.GetHeader().GetDifficulty())
          << ", Timestamp: " << dsblock.GetHeader().GetTimestamp());

  // Update the rand1 value for next PoW
  m_mediator.UpdateDSBlockRand();

  // Store DS Block to disk
  vector<unsigned char> serializedDSBlock;
  dsblock.Serialize(serializedDSBlock, 0);

  BlockStorage::GetBlockStorage().PutDSBlock(dsblock.GetHeader().GetBlockNum(),
                                             serializedDSBlock);
  m_mediator.m_ds->m_latestActiveDSBlockNum = dsblock.GetHeader().GetBlockNum();
  BlockStorage::GetBlockStorage().PutMetadata(
      LATESTACTIVEDSBLOCKNUM, DataConversion::StringToCharArray(to_string(
                                  m_mediator.m_ds->m_latestActiveDSBlockNum)));

  LOG_GENERAL(INFO, "[DSVerif]"
                        << "Storing ds block in index chain");
  uint64_t latestInd = m_mediator.m_blocklinkchain.GetLatestIndex() + 1;
  m_mediator.m_blocklinkchain.AddBlockLink(
      latestInd, dsblock.GetHeader().GetBlockNum(), BlockType::DS,
      dsblock.GetBlockHash());
}

void Node::UpdateDSCommiteeComposition() {
  LOG_MARKER();
  const map<PubKey, Peer> NewDSMembers =
      m_mediator.m_dsBlockChain.GetLastBlock().GetHeader().GetDSPoWWinners();
  for (const auto& DSPowWinner : NewDSMembers) {
    if (m_mediator.m_selfKey.second == DSPowWinner.first) {
      m_mediator.m_DSCommittee->emplace_front(m_mediator.m_selfKey.second,
                                              Peer());
    } else {
      m_mediator.m_DSCommittee->emplace_front(DSPowWinner);
    }
    m_mediator.m_DSCommittee->pop_back();
  }
}

bool Node::CheckWhetherDSBlockNumIsLatest(const uint64_t dsblockNum) {
  LOG_MARKER();

  uint64_t latestBlockNumInBlockchain =
      m_mediator.m_dsBlockChain.GetLastBlock().GetHeader().GetBlockNum();

  if (dsblockNum < latestBlockNumInBlockchain + 1) {
    LOG_EPOCH(WARNING, to_string(m_mediator.m_currentEpochNum).c_str(),
              "We are processing duplicated blocks\n"
                  << "cur block num: " << latestBlockNumInBlockchain << "\n"
                  << "incoming block num: " << dsblockNum);
    return false;
  } else if (dsblockNum > latestBlockNumInBlockchain + 1) {
    LOG_EPOCH(WARNING, to_string(m_mediator.m_currentEpochNum).c_str(),
              "Missing of some DS blocks. Requested: "
                  << dsblockNum
                  << " while Present: " << latestBlockNumInBlockchain);
    // Todo: handle missing DS blocks.
    return false;
  }

  return true;
}

bool Node::VerifyDSBlockCoSignature(const DSBlock& dsblock) {
  LOG_MARKER();

  unsigned int index = 0;
  unsigned int count = 0;

  const vector<bool>& B2 = dsblock.GetB2();
  if (m_mediator.m_DSCommittee->size() != B2.size()) {
    LOG_GENERAL(WARNING, "Mismatch: DS committee size = "
                             << m_mediator.m_DSCommittee->size()
                             << ", co-sig bitmap size = " << B2.size());
    return false;
  }

  // Generate the aggregated key
  vector<PubKey> keys;
  for (auto const& kv : *m_mediator.m_DSCommittee) {
    if (B2.at(index)) {
      keys.emplace_back(kv.first);
      count++;
    }
    index++;
  }

  if (count != ConsensusCommon::NumForConsensus(B2.size())) {
    LOG_GENERAL(WARNING, "Cosig was not generated by enough nodes");
    return false;
  }

  shared_ptr<PubKey> aggregatedKey = MultiSig::AggregatePubKeys(keys);
  if (aggregatedKey == nullptr) {
    LOG_GENERAL(WARNING, "Aggregated key generation failed");
    return false;
  }

  // Verify the collective signature
  vector<unsigned char> message;
  if (!dsblock.GetHeader().Serialize(message, 0)) {
    LOG_GENERAL(WARNING, "DSBlockHeader serialization failed");
    return false;
  }
  dsblock.GetCS1().Serialize(message, message.size());
  BitVector::SetBitVector(message, message.size(), dsblock.GetB1());
  if (!Schnorr::GetInstance().Verify(message, 0, message.size(),
                                     dsblock.GetCS2(), *aggregatedKey)) {
    LOG_GENERAL(WARNING, "Cosig verification failed");
    for (auto& kv : keys) {
      LOG_GENERAL(WARNING, kv);
    }
    return false;
  }

  return true;
}

void Node::LogReceivedDSBlockDetails([[gnu::unused]] const DSBlock& dsblock) {
  LOG_EPOCH(INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
            "dsblock.GetHeader().GetDifficulty(): "
                << (int)dsblock.GetHeader().GetDifficulty());
  LOG_EPOCH(INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
            "dsblock.GetHeader().GetBlockNum(): "
                << dsblock.GetHeader().GetBlockNum());
  LOG_EPOCH(INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
            "dsblock.GetHeader().GetLeaderPubKey(): "
                << dsblock.GetHeader().GetLeaderPubKey());

  LOG_EPOCH(INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
            "Incoming DS committee members");
  for (const auto& dsmember : dsblock.GetHeader().GetDSPoWWinners()) {
    LOG_EPOCH(INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
              dsmember.second);
  }
}

bool Node::LoadShardingStructure() {
  if (LOOKUP_NODE_MODE) {
    LOG_GENERAL(WARNING,
                "Node::LoadShardingStructure not expected to be called "
                "from LookUp node.");
    return true;
  }

  m_numShards = m_mediator.m_ds->m_shards.size();

  // Check the shard ID against the deserialized structure
  if (m_myshardId >= m_mediator.m_ds->m_shards.size()) {
    LOG_EPOCH(WARNING, to_string(m_mediator.m_currentEpochNum).c_str(),
              "Shard ID " << m_myshardId << " >= num shards "
                          << m_mediator.m_ds->m_shards.size());
    return false;
  }

  const auto& my_shard = m_mediator.m_ds->m_shards.at(m_myshardId);

  // m_myShardMembers->clear();
  m_myShardMembers.reset(new std::deque<pair<PubKey, Peer>>);

  // All nodes; first entry is leader
  unsigned int index = 0;
  bool foundMe = false;
  for (const auto& shardNode : my_shard) {
    m_myShardMembers->emplace_back(std::get<SHARD_NODE_PUBKEY>(shardNode),
                                   std::get<SHARD_NODE_PEER>(shardNode));

    // Zero out my IP to avoid sending to myself
    if (m_mediator.m_selfPeer == m_myShardMembers->back().second) {
      m_consensusMyID = index;  // Set my ID
      m_myShardMembers->back().second = Peer();
      foundMe = true;
    }

    LOG_EPOCH(
        INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
        " PubKey: " << DataConversion::SerializableToHexStr(
                           m_myShardMembers->back().first)
                    << " IP: "
                    << m_myShardMembers->back().second.GetPrintableIPAddress()
                    << " Port: "
                    << m_myShardMembers->back().second.m_listenPortHost);

    index++;
  }

  if (!foundMe) {
    LOG_GENERAL(WARNING, "I'm not in the sharding structure, why?");
    RejoinAsNormal();
    return false;
  }

  return true;
}

void Node::LoadTxnSharingInfo() {
  if (LOOKUP_NODE_MODE) {
    LOG_GENERAL(WARNING,
                "Node::LoadTxnSharingInfo not expected to be called from "
                "LookUp node.");
    return;
  }

  LOG_MARKER();

  m_txnSharingIAmSender = false;
  m_txnSharingIAmForwarder = false;
  m_txnSharingAssignedNodes.clear();

  // m_txnSharingAssignedNodes below is basically just the combination of
  // ds_receivers, shard_receivers, and shard_senders We will get rid of this
  // inefficiency eventually

  m_txnSharingAssignedNodes.emplace_back();

  for (auto& m_DSReceiver : m_mediator.m_ds->m_DSReceivers) {
    m_txnSharingAssignedNodes.back().emplace_back(m_DSReceiver);
  }

  for (unsigned int i = 0; i < m_mediator.m_ds->m_shardReceivers.size(); i++) {
    m_txnSharingAssignedNodes.emplace_back();

    for (unsigned int j = 0; j < m_mediator.m_ds->m_shardReceivers.at(i).size();
         j++) {
      m_txnSharingAssignedNodes.back().emplace_back(
          m_mediator.m_ds->m_shardReceivers.at(i).at(j));

      if ((i == m_myshardId) &&
          (m_txnSharingAssignedNodes.back().back() == m_mediator.m_selfPeer)) {
        m_txnSharingIAmForwarder = true;
      }
    }

    m_txnSharingAssignedNodes.emplace_back();

    for (unsigned int j = 0; j < m_mediator.m_ds->m_shardSenders.at(i).size();
         j++) {
      m_txnSharingAssignedNodes.back().emplace_back(
          m_mediator.m_ds->m_shardSenders.at(i).at(j));

      if ((i == m_myshardId) &&
          (m_txnSharingAssignedNodes.back().back() == m_mediator.m_selfPeer)) {
        m_txnSharingIAmSender = true;
      }
    }
  }
}

void Node::StartFirstTxEpoch() {
  if (LOOKUP_NODE_MODE) {
    LOG_GENERAL(WARNING,
                "Node::StartFirstTxEpoch not expected to be called from "
                "LookUp node.");
    return;
  }

  LOG_MARKER();

  ResetConsensusId();

  uint16_t lastBlockHash = 0;
  if (m_mediator.m_currentEpochNum > 1) {
    lastBlockHash = DataConversion::charArrTo16Bits(
        m_mediator.m_txBlockChain.GetLastBlock().GetBlockHash().asBytes());
  }
  m_consensusLeaderID = lastBlockHash % m_myShardMembers->size();

  // Check if I am the leader or backup of the shard
  if (m_mediator.m_selfKey.second ==
      (*m_myShardMembers)[m_consensusLeaderID].first) {
    m_isPrimary = true;
    LOG_EPOCH(INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
              "I am leader of the sharded committee");

    LOG_STATE("[IDENT][" << std::setw(15) << std::left
                         << m_mediator.m_selfPeer.GetPrintableIPAddress()
                         << "][" << m_myshardId << "][0  ] SCLD");
  } else {
    m_isPrimary = false;

    LOG_EPOCH(INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
              "I am backup member of the sharded committee");

    LOG_STATE(
        "[SHSTU]["
        << setw(15) << left << m_mediator.m_selfPeer.GetPrintableIPAddress()
        << "]["
        << m_mediator.m_txBlockChain.GetLastBlock().GetHeader().GetBlockNum() +
               1
        << "] RECEIVED SHARDING STRUCTURE");

    LOG_STATE("[IDENT][" << std::setw(15) << std::left
                         << m_mediator.m_selfPeer.GetPrintableIPAddress()
                         << "][" << m_myshardId << "][" << std::setw(3)
                         << std::left << m_consensusMyID << "] SCBK");
  }

  // Choose N other nodes to be sender of microblock to ds committee.
  // TODO: Randomly choose these nodes?
  m_isMBSender = false;
  unsigned int numOfMBSender = NUM_MICROBLOCK_SENDERS;
  if (m_myShardMembers->size() < numOfMBSender) {
    numOfMBSender = m_myShardMembers->size();
  }

  // Shard leader will not have the flag set
  for (unsigned int i = 1; i < numOfMBSender; i++) {
    if (m_mediator.m_selfKey.second == m_myShardMembers->at(i).first) {
      // Selected node to be sender of its shard's micrblock
      m_isMBSender = true;
      break;
    }
  }

  // Choose N other DS nodes to be recipient of microblock
  m_DSMBReceivers.clear();
  unsigned int numOfMBReceivers =
      std::min(NUM_MICROBLOCK_GOSSIP_RECEIVERS,
               (uint32_t)m_mediator.m_DSCommittee->size());

  for (unsigned int i = 0; i < numOfMBReceivers; i++) {
    m_DSMBReceivers.emplace_back(m_mediator.m_DSCommittee->at(i).second);
  }

  m_justDidFallback = false;
  CommitTxnPacketBuffer();

  if (BROADCAST_GOSSIP_MODE) {
    std::vector<Peer> peers;
    for (const auto& i : *m_myShardMembers) {
      if (i.second.m_listenPortHost != 0) {
        peers.emplace_back(i.second);
      }
    }

    // Initialize every start of DS Epoch
    P2PComm::GetInstance().InitializeRumorManager(peers);
  }

  auto main_func3 = [this]() mutable -> void { RunConsensusOnMicroBlock(); };

  DetachedFunction(1, main_func3);

  FallbackTimerLaunch();
  FallbackTimerPulse();
}

void Node::ResetConsensusId() {
  m_mediator.m_consensusID = m_mediator.m_currentEpochNum == 1 ? 1 : 0;
}

bool Node::ProcessVCDSBlocksMessage(const vector<unsigned char>& message,
                                    unsigned int cur_offset,
                                    [[gnu::unused]] const Peer& from) {
  LOG_MARKER();
  lock_guard<mutex> g(m_mutexDSBlock);

  if (!LOOKUP_NODE_MODE) {
    if (!CheckState(PROCESS_DSBLOCK)) {
      return false;
    }

    // For running from genesis
    if (m_mediator.m_lookup->m_syncType != SyncType::NO_SYNC) {
      m_mediator.m_lookup->m_syncType = SyncType::NO_SYNC;
      if (m_fromNewProcess) {
        m_fromNewProcess = false;
      }

      // Are these necessary? Commented out for now
      // AccountStore::GetInstance().MoveUpdatesToDisk();
      // m_runFromLate = false;
    }
  } else {
    LOG_EPOCH(INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
              "I the lookup node have received the DS Block");
  }

  DSBlock dsblock;
  vector<VCBlock> vcBlocks;
  uint32_t shardId;
  Peer newleaderIP;

  m_mediator.m_ds->m_shards.clear();
  m_mediator.m_ds->m_DSReceivers.clear();
  m_mediator.m_ds->m_shardReceivers.clear();
  m_mediator.m_ds->m_shardSenders.clear();

  if (!Messenger::GetNodeVCDSBlocksMessage(
          message, cur_offset, shardId, dsblock, vcBlocks,
          m_mediator.m_ds->m_shards, m_mediator.m_ds->m_DSReceivers,
          m_mediator.m_ds->m_shardReceivers, m_mediator.m_ds->m_shardSenders)) {
    LOG_EPOCH(WARNING, to_string(m_mediator.m_currentEpochNum).c_str(),
              "Messenger::GetNodeVCDSBlocksMessage failed.");
    return false;
  }

  // Verify the DSBlockHashSet member of the DSBlockHeader
  ShardingHash shardingHash;
  if (!Messenger::GetShardingStructureHash(m_mediator.m_ds->m_shards,
                                           shardingHash)) {
    LOG_EPOCH(WARNING, to_string(m_mediator.m_currentEpochNum).c_str(),
              "Messenger::GetShardingStructureHash failed.");
    return false;
  }
  if (shardingHash != dsblock.GetHeader().GetShardingHash()) {
    LOG_GENERAL(WARNING,
                "Sharding structure hash in newly received DS Block doesn't "
                "match. Calculated: "
                    << shardingHash
                    << " Received: " << dsblock.GetHeader().GetShardingHash());
    return false;
  }
  TxSharingHash txSharingHash;
  if (!Messenger::GetTxSharingAssignmentsHash(
          m_mediator.m_ds->m_DSReceivers, m_mediator.m_ds->m_shardReceivers,
          m_mediator.m_ds->m_shardSenders, txSharingHash)) {
    LOG_EPOCH(WARNING, to_string(m_mediator.m_currentEpochNum).c_str(),
              "Messenger::GetTxSharingAssignmentsHash failed.");
    return false;
  }
  if (txSharingHash != dsblock.GetHeader().GetTxSharingHash()) {
    LOG_GENERAL(WARNING,
                "Tx sharing structure hash in newly received DS Block doesn't "
                "match. Calculated: "
                    << txSharingHash
                    << " Received: " << dsblock.GetHeader().GetTxSharingHash());
    return false;
  }

  // Verify the CommitteeHash member of the BlockHeaderBase
  CommitteeHash committeeHash;
  if (!Messenger::GetDSCommitteeHash(*m_mediator.m_DSCommittee,
                                     committeeHash)) {
    LOG_EPOCH(WARNING, to_string(m_mediator.m_currentEpochNum).c_str(),
              "Messenger::GetDSCommitteeHash failed.");
    return false;
  }
  if (committeeHash != dsblock.GetHeader().GetCommitteeHash()) {
    LOG_GENERAL(WARNING,
                "DS committee hash in newly received DS Block doesn't match. "
                "Calculated: "
                    << committeeHash
                    << " Received: " << dsblock.GetHeader().GetCommitteeHash());
    return false;
  }

  m_myshardId = shardId;

  LogReceivedDSBlockDetails(dsblock);

  BlockHash temp_blockHash = dsblock.GetHeader().GetMyHash();
  if (temp_blockHash != dsblock.GetBlockHash()) {
    LOG_GENERAL(WARNING,
                "Block Hash in Newly received DS Block doesn't match. "
                "Calculated: "
                    << temp_blockHash
                    << " Received: " << dsblock.GetBlockHash().hex());
    return false;
  }

  // Checking for freshness of incoming DS Block
  if (!CheckWhetherDSBlockNumIsLatest(dsblock.GetHeader().GetBlockNum())) {
    return false;
  }

  uint32_t expectedViewChangeCounter = 1;
  for (const auto& vcBlock : vcBlocks) {
    if (vcBlock.GetHeader().GetViewChangeCounter() !=
        expectedViewChangeCounter) {
      LOG_GENERAL(WARNING, "Unexpected VC block counter. Expected: "
                               << expectedViewChangeCounter << " Received: "
                               << vcBlock.GetHeader().GetViewChangeCounter());
    }

    if (!ProcessVCBlockCore(vcBlock)) {
      LOG_GENERAL(WARNING, "Checking for error when processing vc blocknum "
                               << vcBlock.GetHeader().GetViewChangeCounter());
      return false;
    }

    LOG_GENERAL(INFO, "view change completed for vc blocknum "
                          << vcBlock.GetHeader().GetViewChangeCounter());
    expectedViewChangeCounter++;
  }

  // Check the signature of this DS block
  if (!VerifyDSBlockCoSignature(dsblock)) {
    LOG_EPOCH(WARNING, to_string(m_mediator.m_currentEpochNum).c_str(),
              "DSBlock co-sig verification failed");
    return false;
  }

  auto func = [this, dsblock]() mutable -> void {
    lock_guard<mutex> g(m_mediator.m_mutexCurSWInfo);
    if (m_mediator.m_curSWInfo != dsblock.GetHeader().GetSWInfo()) {
      if (UpgradeManager::GetInstance().DownloadSW()) {
        m_mediator.m_curSWInfo =
            *UpgradeManager::GetInstance().GetLatestSWInfo();
      }
    }
  };
  DetachedFunction(1, func);

  // Add to block chain and Store the DS block to disk.
  StoreDSBlockToDisk(dsblock);

  LOG_STATE(
      "[DSBLK]["
      << setw(15) << left << m_mediator.m_selfPeer.GetPrintableIPAddress()
      << "]["
      << m_mediator.m_txBlockChain.GetLastBlock().GetHeader().GetBlockNum() + 1
      << "] RECEIVED DSBLOCK");

  if (LOOKUP_NODE_MODE) {
    LOG_EPOCH(INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
              "I the lookup node have stored the DS Block");
  }

  m_mediator.UpdateDSBlockRand();  // Update the rand1 value for next PoW
  UpdateDSCommiteeComposition();

  if (!LOOKUP_NODE_MODE) {
    uint32_t ds_size = m_mediator.m_DSCommittee->size();
    POW::GetInstance().StopMining();
    m_stillMiningPrimary = false;

    // Assign from size -1 as it will get pop and push into ds committee data
    // structure, Hence, the ordering is reverse.
    const map<PubKey, Peer> dsPoWWinners =
        m_mediator.m_dsBlockChain.GetLastBlock().GetHeader().GetDSPoWWinners();
    unsigned int newDSMemberIndex = dsPoWWinners.size() - 1;
    bool isNewDSMember = false;

    for (const auto& newDSMember : dsPoWWinners) {
      if (m_mediator.m_selfKey.second == newDSMember.first) {
        isNewDSMember = true;
        m_mediator.m_ds->m_consensusMyID = newDSMemberIndex;
        LOG_EPOCH(INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
                  "I won DS PoW. Currently, one of the new ds "
                  "committee member with id "
                      << m_mediator.m_ds->m_consensusMyID);
      }
      newDSMemberIndex--;
    }

    uint16_t lastBlockHash = 0;
    if (m_mediator.m_currentEpochNum > 1) {
      lastBlockHash = DataConversion::charArrTo16Bits(
          m_mediator.m_txBlockChain.GetLastBlock().GetBlockHash().asBytes());
    }
    m_mediator.m_ds->m_consensusLeaderID = lastBlockHash % ds_size;

    // If I am the next DS leader -> need to set myself up as a DS node
    if (isNewDSMember) {
      // Process sharding structure as a DS node
      if (!m_mediator.m_ds->ProcessShardingStructure(
              m_mediator.m_ds->m_shards,
              m_mediator.m_ds->m_publicKeyToshardIdMap,
              m_mediator.m_ds->m_mapNodeReputation)) {
        return false;
      }

      // Process txn sharing assignments as a DS node
      m_mediator.m_ds->ProcessTxnBodySharingAssignment();

      //(We're getting rid of this eventually Clean up my txns coz I am DS)
      m_mediator.m_node->CleanCreatedTransaction();

      {
        lock_guard<mutex> g(m_mediator.m_mutexDSCommittee);
        LOG_GENERAL(INFO,
                    "DS leader is at " << m_mediator.m_ds->m_consensusLeaderID);
        if (m_mediator.m_ds->m_consensusLeaderID ==
            m_mediator.m_ds->m_consensusMyID) {
          // I am the new DS committee leader
          m_mediator.m_ds->m_mode = DirectoryService::Mode::PRIMARY_DS;
          LOG_EPOCHINFO(to_string(m_mediator.m_currentEpochNum).c_str(),
                        DS_LEADER_MSG);
          LOG_STATE("[IDENT][" << std::setw(15) << std::left
                               << m_mediator.m_selfPeer.GetPrintableIPAddress()
                               << "][0     ] DSLD");
        } else {
          m_mediator.m_ds->m_mode = DirectoryService::Mode::BACKUP_DS;
          LOG_EPOCHINFO(to_string(m_mediator.m_currentEpochNum).c_str(),
                        DS_BACKUP_MSG);
        }
      }

      m_mediator.m_ds->StartFirstTxEpoch();
      return true;
    } else {
      // If I am a shard node
      LOG_EPOCH(INFO, to_string(m_mediator.m_currentEpochNum).c_str(),
                "I lost PoW (DS level) :-( Better luck next time!");

      // Process sharding structure as a shard node
      if (!LoadShardingStructure()) {
        return false;
      }

      if (BROADCAST_TREEBASED_CLUSTER_MODE) {
        SendDSBlockToOtherShardNodes(message);
      }

      // Process txn sharing assignments as a shard node
      LoadTxnSharingInfo();

      // Finally, start as a shard node
      StartFirstTxEpoch();
    }
  } else {
    // Process sharding structure as a lookup node
    m_mediator.m_lookup->ProcessEntireShardingStructure();

    ResetConsensusId();

    if (m_mediator.m_lookup->GetIsServer() && USE_REMOTE_TXN_CREATOR) {
      m_mediator.m_lookup->SenderTxnBatchThread();
    }

    FallbackTimerLaunch();
    FallbackTimerPulse();
  }

  LOG_GENERAL(INFO, "DS committee");
  for (const auto& member : *m_mediator.m_DSCommittee) {
    LOG_GENERAL(INFO, member.second);
  }

  return true;
}

void Node::SendDSBlockToOtherShardNodes(
    const vector<unsigned char>& dsblock_message) {
  LOG_MARKER();
  LOG_GENERAL(INFO,
              "Primary CLUSTER SIZE used is "
              "(NUM_FORWARDED_BLOCK_RECEIVERS_PER_SHARD):"
                  << NUM_FORWARDED_BLOCK_RECEIVERS_PER_SHARD);
  SendBlockToOtherShardNodes(dsblock_message,
                             NUM_FORWARDED_BLOCK_RECEIVERS_PER_SHARD,
                             NUM_OF_TREEBASED_CHILD_CLUSTERS);
}