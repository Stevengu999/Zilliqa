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

#ifndef __MEDIATOR_H__
#define __MEDIATOR_H__

#include <deque>

#include "libCrypto/Schnorr.h"
#include "libDB/Archival.h"
#include "libDB/BaseDB.h"
#include "libData/BlockChainData/BlockChain.h"
#include "libData/BlockChainData/BlockLinkChain.h"
#include "libDirectoryService/DirectoryService.h"
#include "libLookup/Lookup.h"
#include "libNetwork/Peer.h"
#include "libNode/Node.h"
#include "libValidator/Validator.h"

/// A mediator class for providing access to global members.
class Mediator {
  std::mutex m_mutexHeartBeat;
  unsigned int m_heartBeatTime;

 public:
  /// The Zilliqa instance's key pair.
  std::pair<PrivKey, PubKey> m_selfKey;

  /// The Zilliqa instance's IP information.
  Peer m_selfPeer;

  /// The reference to the DirectoryService instance.
  DirectoryService* m_ds;

  /// The reference to the Node instance.
  Node* m_node;

  /// The reference to the Lookup instance.
  Lookup* m_lookup;

  /// Pointer to the Validator instance.
  ValidatorBase* m_validator;

  // Archive DB pointer
  BaseDB* m_archDB;

  // Archival Node pointer
  Archival* m_archival;
  /// The transient DS blockchain.
  DSBlockChain m_dsBlockChain;

  /// The transient Tx blockchain.
  TxBlockChain m_txBlockChain;

  /// IndexBlockChain for linking ds/vc/fb blocks
  BlockLinkChain m_blocklinkchain;

  /// The current epoch.
  uint64_t m_currentEpochNum = 0;

#ifdef HEARTBEAT_TEST
  bool m_killPulse = false;
#endif  // HEARTBEAT_TEST

  /// The consensus ID
  uint32_t m_consensusID;

  // DS committee members
  // Fixed-sized double-ended queue depending on size of DS committee at
  // bootstrap Leader is at head of queue PoW winner will be pushed in at head
  // of queue (new leader) Oldest member will be pushed out from tail of queue

  /// The public keys and current members of the DS committee.
  std::shared_ptr<std::deque<std::pair<PubKey, Peer>>> m_DSCommittee;
  std::mutex m_mutexDSCommittee;

  /// The current epoch randomness from the DS blockchain.
  std::array<unsigned char, POW_SIZE> m_dsBlockRand;

  /// The current epoch randomness from the Tx blockchain.
  std::array<unsigned char, POW_SIZE> m_txBlockRand;

  /// To determine if the node successfully recovered from persistence
  bool m_isRetrievedHistory;

  /// Flag for indicating whether it's vacuous epoch now
  bool m_isVacuousEpoch;
  std::mutex m_mutexVacuousEpoch;

  /// Record current software information which already downloaded to this node
  SWInfo m_curSWInfo;
  std::mutex m_mutexCurSWInfo;

  /// Constructor.
  Mediator(const std::pair<PrivKey, PubKey>& key, const Peer& peer);

  /// Destructor.
  ~Mediator();

  /// Sets the references to the subclass instances.
  void RegisterColleagues(DirectoryService* ds, Node* node, Lookup* lookup,
                          ValidatorBase* validator, BaseDB* archDB = nullptr,
                          Archival* arch = nullptr);

  /// Updates the DS blockchain random for PoW.
  void UpdateDSBlockRand(bool isGenesis = false);

  /// Updates the Tx blockchain random for PoW.
  void UpdateTxBlockRand(bool isGenesis = false);

  std::string GetNodeMode(const Peer& peer);

  /// Launches the heartbeat monitoring thread
  void HeartBeatLaunch();

  /// Resets the heartbeat counter (to indicate liveness)
  void HeartBeatPulse();

  void IncreaseEpochNum();

  bool GetIsVacuousEpoch();

  uint32_t GetShardSize(const bool& useShardStructure) const;
};

#endif  // __MEDIATOR_H__
