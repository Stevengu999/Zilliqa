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

#include "Messenger.h"
#include "libData/AccountData/AccountStore.h"
#include "libData/AccountData/Transaction.h"
#include "libData/BlockChainData/BlockLinkChain.h"
#include "libMessage/ZilliqaMessage.pb.h"
#include "libUtils/Logger.h"

#include <algorithm>
#include <map>
#include <random>
#include <unordered_set>

using namespace boost::multiprecision;
using namespace std;
using namespace ZilliqaMessage;

namespace {
void SerializableToProtobufByteArray(const Serializable& serializable,
                                     ByteArray& byteArray) {
  vector<unsigned char> tmp;
  serializable.Serialize(tmp, 0);
  byteArray.set_data(tmp.data(), tmp.size());
}

void ProtobufByteArrayToSerializable(const ByteArray& byteArray,
                                     Serializable& serializable) {
  vector<unsigned char> tmp;
  copy(byteArray.data().begin(), byteArray.data().end(), back_inserter(tmp));
  serializable.Deserialize(tmp, 0);
}

// Temporary function for use by data blocks
void SerializableToProtobufByteArray(const SerializableDataBlock& serializable,
                                     ByteArray& byteArray) {
  vector<unsigned char> tmp;
  serializable.Serialize(tmp, 0);
  byteArray.set_data(tmp.data(), tmp.size());
}

// Temporary function for use by data blocks
void ProtobufByteArrayToSerializable(const ByteArray& byteArray,
                                     SerializableDataBlock& serializable) {
  vector<unsigned char> tmp;
  copy(byteArray.data().begin(), byteArray.data().end(), back_inserter(tmp));
  serializable.Deserialize(tmp, 0);
}

template <class T, size_t S>
void NumberToProtobufByteArray(const T& number, ByteArray& byteArray) {
  vector<unsigned char> tmp;
  Serializable::SetNumber<T>(tmp, 0, number, S);
  byteArray.set_data(tmp.data(), tmp.size());
}

template <class T, size_t S>
void ProtobufByteArrayToNumber(const ByteArray& byteArray, T& number) {
  vector<unsigned char> tmp;
  copy(byteArray.data().begin(), byteArray.data().end(), back_inserter(tmp));
  number = Serializable::GetNumber<T>(tmp, 0, S);
}

void DSCommitteeToProtobuf(const deque<pair<PubKey, Peer>>& dsCommittee,
                           ProtoDSCommittee& protoDSCommittee) {
  for (const auto& node : dsCommittee) {
    ProtoDSNode* protodsnode = protoDSCommittee.add_dsnodes();
    SerializableToProtobufByteArray(node.first, *protodsnode->mutable_pubkey());
    SerializableToProtobufByteArray(node.second, *protodsnode->mutable_peer());
  }
}

void ProtobufToDSCommittee(const ProtoDSCommittee& protoDSCommittee,
                           deque<pair<PubKey, Peer>>& dsCommittee) {
  for (const auto& dsnode : protoDSCommittee.dsnodes()) {
    PubKey pubkey;
    Peer peer;

    ProtobufByteArrayToSerializable(dsnode.pubkey(), pubkey);
    ProtobufByteArrayToSerializable(dsnode.peer(), peer);
    dsCommittee.emplace_back(pubkey, peer);
  }
}

void DSCommitteeToProtoCommittee(const deque<pair<PubKey, Peer>>& dsCommittee,
                                 ProtoCommittee& protoCommittee) {
  for (const auto& node : dsCommittee) {
    SerializableToProtobufByteArray(node.first, *protoCommittee.add_members());
  }
}

void ShardToProtoCommittee(const Shard& shard, ProtoCommittee& protoCommittee) {
  for (const auto& node : shard) {
    SerializableToProtobufByteArray(std::get<SHARD_NODE_PUBKEY>(node),
                                    *protoCommittee.add_members());
  }
}

void ShardingStructureToProtobuf(
    const DequeOfShard& shards,
    ProtoShardingStructure& protoShardingStructure) {
  for (const auto& shard : shards) {
    ProtoShardingStructure::Shard* proto_shard =
        protoShardingStructure.add_shards();

    for (const auto& node : shard) {
      ProtoShardingStructure::Member* proto_member = proto_shard->add_members();

      SerializableToProtobufByteArray(std::get<SHARD_NODE_PUBKEY>(node),
                                      *proto_member->mutable_pubkey());
      SerializableToProtobufByteArray(std::get<SHARD_NODE_PEER>(node),
                                      *proto_member->mutable_peerinfo());
      proto_member->set_reputation(std::get<SHARD_NODE_REP>(node));
    }
  }
}

void ProtobufToShardingStructure(
    const ProtoShardingStructure& protoShardingStructure,
    DequeOfShard& shards) {
  for (const auto& proto_shard : protoShardingStructure.shards()) {
    shards.emplace_back();

    for (const auto& proto_member : proto_shard.members()) {
      PubKey key;
      Peer peer;

      ProtobufByteArrayToSerializable(proto_member.pubkey(), key);
      ProtobufByteArrayToSerializable(proto_member.peerinfo(), peer);

      shards.back().emplace_back(key, peer, proto_member.reputation());
    }
  }
}

void TxSharingAssignmentsToProtobuf(
    const vector<Peer>& dsReceivers, const vector<vector<Peer>>& shardReceivers,
    const vector<vector<Peer>>& shardSenders,
    ProtoTxSharingAssignments& protoTxSharingAssignments) {
  for (const auto& dsnode : dsReceivers) {
    SerializableToProtobufByteArray(dsnode,
                                    *protoTxSharingAssignments.add_dsnodes());
  }

  for (unsigned int i = 0; i < shardReceivers.size(); i++) {
    ProtoTxSharingAssignments::AssignedNodes* proto_shard =
        protoTxSharingAssignments.add_shardnodes();

    for (const auto& receiver : shardReceivers.at(i)) {
      SerializableToProtobufByteArray(receiver, *proto_shard->add_receivers());
    }
    for (const auto& sender : shardSenders.at(i)) {
      SerializableToProtobufByteArray(sender, *proto_shard->add_senders());
    }
  }
}

void ProtobufToTxSharingAssignments(
    const ProtoTxSharingAssignments& protoTxSharingAssignments,
    vector<Peer>& dsReceivers, vector<vector<Peer>>& shardReceivers,
    vector<vector<Peer>>& shardSenders) {
  for (const auto& dsnode : protoTxSharingAssignments.dsnodes()) {
    Peer peer;
    ProtobufByteArrayToSerializable(dsnode, peer);
    dsReceivers.emplace_back(peer);
  }

  for (const auto& proto_shard : protoTxSharingAssignments.shardnodes()) {
    shardReceivers.emplace_back();

    for (const auto& receiver : proto_shard.receivers()) {
      Peer peer;
      ProtobufByteArrayToSerializable(receiver, peer);
      shardReceivers.back().emplace_back(peer);
    }

    shardSenders.emplace_back();

    for (const auto& sender : proto_shard.senders()) {
      Peer peer;
      ProtobufByteArrayToSerializable(sender, peer);
      shardSenders.back().emplace_back(peer);
    }
  }
}

void DSBlockHeaderToProtobuf(const DSBlockHeader& dsBlockHeader,
                             ProtoDSBlock::DSBlockHeader& protoDSBlockHeader) {
  protoDSBlockHeader.set_dsdifficulty(dsBlockHeader.GetDSDifficulty());
  protoDSBlockHeader.set_difficulty(dsBlockHeader.GetDifficulty());
  protoDSBlockHeader.set_prevhash(dsBlockHeader.GetPrevHash().data(),
                                  dsBlockHeader.GetPrevHash().size);
  SerializableToProtobufByteArray(dsBlockHeader.GetLeaderPubKey(),
                                  *protoDSBlockHeader.mutable_leaderpubkey());

  protoDSBlockHeader.set_blocknum(dsBlockHeader.GetBlockNum());
  NumberToProtobufByteArray<uint256_t, UINT256_SIZE>(
      dsBlockHeader.GetTimestamp(), *protoDSBlockHeader.mutable_timestamp());
  SerializableToProtobufByteArray(dsBlockHeader.GetSWInfo(),
                                  *protoDSBlockHeader.mutable_swinfo());

  ZilliqaMessage::ProtoDSBlock::DSBlockHeader::PowDSWinners* powdswinner;

  for (const auto& winner : dsBlockHeader.GetDSPoWWinners()) {
    powdswinner = protoDSBlockHeader.add_dswinners();
    SerializableToProtobufByteArray(winner.first, *powdswinner->mutable_key());
    SerializableToProtobufByteArray(winner.second, *powdswinner->mutable_val());
  }

  ZilliqaMessage::ProtoDSBlock::DSBlockHashSet* protoHeaderHash =
      protoDSBlockHeader.mutable_hash();
  protoHeaderHash->set_shardinghash(dsBlockHeader.GetShardingHash().data(),
                                    dsBlockHeader.GetShardingHash().size);
  protoHeaderHash->set_txsharinghash(dsBlockHeader.GetTxSharingHash().data(),
                                     dsBlockHeader.GetTxSharingHash().size);
  protoHeaderHash->set_reservedfield(
      dsBlockHeader.GetHashSetReservedField().data(),
      dsBlockHeader.GetHashSetReservedField().size());

  protoDSBlockHeader.set_committeehash(dsBlockHeader.GetCommitteeHash().data(),
                                       dsBlockHeader.GetCommitteeHash().size);
}

void DSBlockToProtobuf(const DSBlock& dsBlock, ProtoDSBlock& protoDSBlock) {
  // Serialize header

  ZilliqaMessage::ProtoDSBlock::DSBlockHeader* protoHeader =
      protoDSBlock.mutable_header();

  const DSBlockHeader& header = dsBlock.GetHeader();

  DSBlockHeaderToProtobuf(header, *protoHeader);

  // Serialize cosigs

  ZilliqaMessage::ProtoDSBlock::CoSignatures* cosigs =
      protoDSBlock.mutable_cosigs();

  SerializableToProtobufByteArray(dsBlock.GetCS1(), *cosigs->mutable_cs1());
  for (const auto& i : dsBlock.GetB1()) {
    cosigs->add_b1(i);
  }
  SerializableToProtobufByteArray(dsBlock.GetCS2(), *cosigs->mutable_cs2());
  for (const auto& i : dsBlock.GetB2()) {
    cosigs->add_b2(i);
  }

  // Block hash

  protoDSBlock.set_blockhash(dsBlock.GetBlockHash().data(),
                             dsBlock.GetBlockHash().size);
}

void ProtobufToDSBlockHeader(
    const ProtoDSBlock::DSBlockHeader& protoDSBlockHeader,
    DSBlockHeader& dsBlockHeader) {
  BlockHash prevHash;
  PubKey leaderPubKey;
  uint256_t timestamp;
  SWInfo swInfo;
  CommitteeHash committeeHash;

  copy(protoDSBlockHeader.prevhash().begin(),
       protoDSBlockHeader.prevhash().begin() +
           min((unsigned int)protoDSBlockHeader.prevhash().size(),
               (unsigned int)prevHash.size),
       prevHash.asArray().begin());
  ProtobufByteArrayToSerializable(protoDSBlockHeader.leaderpubkey(),
                                  leaderPubKey);
  ProtobufByteArrayToNumber<uint256_t, UINT256_SIZE>(
      protoDSBlockHeader.timestamp(), timestamp);
  ProtobufByteArrayToSerializable(protoDSBlockHeader.swinfo(), swInfo);

  // Deserialize powDSWinners
  map<PubKey, Peer> powDSWinners;
  PubKey tempPubKey;
  Peer tempWinnerNetworkInfo;
  for (const auto& dswinner : protoDSBlockHeader.dswinners()) {
    ProtobufByteArrayToSerializable(dswinner.key(), tempPubKey);
    ProtobufByteArrayToSerializable(dswinner.val(), tempWinnerNetworkInfo);
    powDSWinners[tempPubKey] = tempWinnerNetworkInfo;
  }

  // Deserialize DSBlockHashSet
  DSBlockHashSet hash;
  const ZilliqaMessage::ProtoDSBlock::DSBlockHashSet& protoDSBlockHeaderHash =
      protoDSBlockHeader.hash();
  copy(protoDSBlockHeaderHash.shardinghash().begin(),
       protoDSBlockHeaderHash.shardinghash().begin() +
           min((unsigned int)protoDSBlockHeaderHash.shardinghash().size(),
               (unsigned int)hash.m_shardingHash.size),
       hash.m_shardingHash.asArray().begin());
  copy(protoDSBlockHeaderHash.txsharinghash().begin(),
       protoDSBlockHeaderHash.txsharinghash().begin() +
           min((unsigned int)protoDSBlockHeaderHash.txsharinghash().size(),
               (unsigned int)hash.m_txSharingHash.size),
       hash.m_txSharingHash.asArray().begin());
  copy(protoDSBlockHeaderHash.reservedfield().begin(),
       protoDSBlockHeaderHash.reservedfield().begin() +
           min((unsigned int)protoDSBlockHeaderHash.reservedfield().size(),
               (unsigned int)hash.m_reservedField.size()),
       hash.m_reservedField.begin());

  copy(protoDSBlockHeader.committeehash().begin(),
       protoDSBlockHeader.committeehash().begin() +
           min((unsigned int)protoDSBlockHeader.committeehash().size(),
               (unsigned int)committeeHash.size),
       committeeHash.asArray().begin());

  // Generate the new DSBlock

  dsBlockHeader = DSBlockHeader(
      protoDSBlockHeader.dsdifficulty(), protoDSBlockHeader.difficulty(),
      prevHash, leaderPubKey, protoDSBlockHeader.blocknum(), timestamp, swInfo,
      powDSWinners, hash, committeeHash);
}

void ProtobufToDSBlock(const ProtoDSBlock& protoDSBlock, DSBlock& dsBlock) {
  // Deserialize header

  const ZilliqaMessage::ProtoDSBlock::DSBlockHeader& protoHeader =
      protoDSBlock.header();

  DSBlockHeader header;

  ProtobufToDSBlockHeader(protoHeader, header);

  // Deserialize cosigs
  CoSignatures cosigs;
  cosigs.m_B1.resize(protoDSBlock.cosigs().b1().size());
  cosigs.m_B2.resize(protoDSBlock.cosigs().b2().size());

  ProtobufByteArrayToSerializable(protoDSBlock.cosigs().cs1(), cosigs.m_CS1);
  copy(protoDSBlock.cosigs().b1().begin(), protoDSBlock.cosigs().b1().end(),
       cosigs.m_B1.begin());
  ProtobufByteArrayToSerializable(protoDSBlock.cosigs().cs2(), cosigs.m_CS2);
  copy(protoDSBlock.cosigs().b2().begin(), protoDSBlock.cosigs().b2().end(),
       cosigs.m_B2.begin());

  // Generate the new DSBloc
  dsBlock = DSBlock(header, CoSignatures(cosigs));

  // Deserialize the block hash
  BlockHash blockHash;

  copy(protoDSBlock.blockhash().begin(),
       protoDSBlock.blockhash().begin() +
           min((unsigned int)protoDSBlock.blockhash().size(),
               (unsigned int)blockHash.size),
       blockHash.asArray().begin());

  dsBlock.SetBlockHash(blockHash);
}

void MicroBlockHeaderToProtobuf(
    const MicroBlockHeader& microBlockHeader,
    ProtoMicroBlock::MicroBlockHeader& protoMicroBlockHeader) {
  protoMicroBlockHeader.set_type(microBlockHeader.GetType());
  protoMicroBlockHeader.set_version(microBlockHeader.GetVersion());
  protoMicroBlockHeader.set_shardid(microBlockHeader.GetShardId());
  NumberToProtobufByteArray<uint256_t, UINT256_SIZE>(
      microBlockHeader.GetGasLimit(),
      *protoMicroBlockHeader.mutable_gaslimit());
  NumberToProtobufByteArray<uint256_t, UINT256_SIZE>(
      microBlockHeader.GetGasUsed(), *protoMicroBlockHeader.mutable_gasused());
  protoMicroBlockHeader.set_prevhash(microBlockHeader.GetPrevHash().data(),
                                     microBlockHeader.GetPrevHash().size);
  protoMicroBlockHeader.set_blocknum(microBlockHeader.GetBlockNum());
  NumberToProtobufByteArray<uint256_t, UINT256_SIZE>(
      microBlockHeader.GetTimestamp(),
      *protoMicroBlockHeader.mutable_timestamp());
  protoMicroBlockHeader.set_txroothash(microBlockHeader.GetTxRootHash().data(),
                                       microBlockHeader.GetTxRootHash().size);
  protoMicroBlockHeader.set_numtxs(microBlockHeader.GetNumTxs());
  SerializableToProtobufByteArray(microBlockHeader.GetMinerPubKey(),
                                  *protoMicroBlockHeader.mutable_minerpubkey());
  protoMicroBlockHeader.set_dsblocknum(microBlockHeader.GetDSBlockNum());
  protoMicroBlockHeader.set_dsblockheader(
      microBlockHeader.GetDSBlockHeader().data(),
      microBlockHeader.GetDSBlockHeader().size);
  protoMicroBlockHeader.set_statedeltahash(
      microBlockHeader.GetStateDeltaHash().data(),
      microBlockHeader.GetStateDeltaHash().size);
  protoMicroBlockHeader.set_tranreceipthash(
      microBlockHeader.GetTranReceiptHash().data(),
      microBlockHeader.GetTranReceiptHash().size);

  protoMicroBlockHeader.set_committeehash(
      microBlockHeader.GetCommitteeHash().data(),
      microBlockHeader.GetCommitteeHash().size);
}

void MicroBlockToProtobuf(const MicroBlock& microBlock,
                          ProtoMicroBlock& protoMicroBlock) {
  // Serialize header

  ZilliqaMessage::ProtoMicroBlock::MicroBlockHeader* protoHeader =
      protoMicroBlock.mutable_header();

  const MicroBlockHeader& header = microBlock.GetHeader();

  MicroBlockHeaderToProtobuf(header, *protoHeader);

  // Serialize body

  for (const auto& hash : microBlock.GetTranHashes()) {
    protoMicroBlock.add_tranhashes(hash.data(), hash.size);
  }

  // Serialize cosigs

  ZilliqaMessage::ProtoMicroBlock::CoSignatures* cosigs =
      protoMicroBlock.mutable_cosigs();

  SerializableToProtobufByteArray(microBlock.GetCS1(), *cosigs->mutable_cs1());
  for (const auto& i : microBlock.GetB1()) {
    cosigs->add_b1(i);
  }
  SerializableToProtobufByteArray(microBlock.GetCS2(), *cosigs->mutable_cs2());
  for (const auto& i : microBlock.GetB2()) {
    cosigs->add_b2(i);
  }
}

void ProtobufToMicroBlockHeader(
    const ProtoMicroBlock::MicroBlockHeader& protoMicroBlockHeader,
    MicroBlockHeader& microBlockHeader) {
  uint256_t gasLimit;
  uint256_t gasUsed;
  BlockHash prevHash;
  uint256_t timestamp;
  TxnHash txRootHash;
  PubKey minerPubKey;
  BlockHash dsBlockHeader;
  StateHash stateDeltaHash;
  TxnHash tranReceiptHash;
  CommitteeHash committeeHash;

  ProtobufByteArrayToNumber<uint256_t, UINT256_SIZE>(
      protoMicroBlockHeader.gaslimit(), gasLimit);
  ProtobufByteArrayToNumber<uint256_t, UINT256_SIZE>(
      protoMicroBlockHeader.gasused(), gasUsed);
  copy(protoMicroBlockHeader.prevhash().begin(),
       protoMicroBlockHeader.prevhash().begin() +
           min((unsigned int)protoMicroBlockHeader.prevhash().size(),
               (unsigned int)prevHash.size),
       prevHash.asArray().begin());
  ProtobufByteArrayToNumber<uint256_t, UINT256_SIZE>(
      protoMicroBlockHeader.timestamp(), timestamp);
  copy(protoMicroBlockHeader.txroothash().begin(),
       protoMicroBlockHeader.txroothash().begin() +
           min((unsigned int)protoMicroBlockHeader.txroothash().size(),
               (unsigned int)txRootHash.size),
       txRootHash.asArray().begin());
  ProtobufByteArrayToSerializable(protoMicroBlockHeader.minerpubkey(),
                                  minerPubKey);
  copy(protoMicroBlockHeader.dsblockheader().begin(),
       protoMicroBlockHeader.dsblockheader().begin() +
           min((unsigned int)protoMicroBlockHeader.dsblockheader().size(),
               (unsigned int)dsBlockHeader.size),
       dsBlockHeader.asArray().begin());
  copy(protoMicroBlockHeader.statedeltahash().begin(),
       protoMicroBlockHeader.statedeltahash().begin() +
           min((unsigned int)protoMicroBlockHeader.statedeltahash().size(),
               (unsigned int)stateDeltaHash.size),
       stateDeltaHash.asArray().begin());
  copy(protoMicroBlockHeader.tranreceipthash().begin(),
       protoMicroBlockHeader.tranreceipthash().begin() +
           min((unsigned int)protoMicroBlockHeader.tranreceipthash().size(),
               (unsigned int)tranReceiptHash.size),
       tranReceiptHash.asArray().begin());

  copy(protoMicroBlockHeader.committeehash().begin(),
       protoMicroBlockHeader.committeehash().begin() +
           min((unsigned int)protoMicroBlockHeader.committeehash().size(),
               (unsigned int)committeeHash.size),
       committeeHash.asArray().begin());

  microBlockHeader = MicroBlockHeader(
      protoMicroBlockHeader.type(), protoMicroBlockHeader.version(),
      protoMicroBlockHeader.shardid(), gasLimit, gasUsed, prevHash,
      protoMicroBlockHeader.blocknum(), timestamp, txRootHash,
      protoMicroBlockHeader.numtxs(), minerPubKey,
      protoMicroBlockHeader.dsblocknum(), dsBlockHeader, stateDeltaHash,
      tranReceiptHash, committeeHash);
}

void ProtobufToMicroBlock(const ProtoMicroBlock& protoMicroBlock,
                          MicroBlock& microBlock) {
  // Deserialize header

  const ZilliqaMessage::ProtoMicroBlock::MicroBlockHeader& protoHeader =
      protoMicroBlock.header();

  MicroBlockHeader header;

  ProtobufToMicroBlockHeader(protoHeader, header);

  // Deserialize body

  vector<TxnHash> tranHashes;
  for (const auto& hash : protoMicroBlock.tranhashes()) {
    tranHashes.emplace_back();
    unsigned int size =
        min((unsigned int)hash.size(), (unsigned int)tranHashes.back().size);
    copy(hash.begin(), hash.begin() + size,
         tranHashes.back().asArray().begin());
  }

  // Deserialize cosigs

  CoSignatures cosigs;
  cosigs.m_B1.resize(protoMicroBlock.cosigs().b1().size());
  cosigs.m_B2.resize(protoMicroBlock.cosigs().b2().size());

  ProtobufByteArrayToSerializable(protoMicroBlock.cosigs().cs1(), cosigs.m_CS1);
  copy(protoMicroBlock.cosigs().b1().begin(),
       protoMicroBlock.cosigs().b1().end(), cosigs.m_B1.begin());
  ProtobufByteArrayToSerializable(protoMicroBlock.cosigs().cs2(), cosigs.m_CS2);
  copy(protoMicroBlock.cosigs().b2().begin(),
       protoMicroBlock.cosigs().b2().end(), cosigs.m_B2.begin());

  // Generate the new MicroBlock

  microBlock = MicroBlock(header, tranHashes, CoSignatures(cosigs));
}

void TxBlockHeaderToProtobuf(const TxBlockHeader& txBlockHeader,
                             ProtoTxBlock::TxBlockHeader& protoTxBlockHeader) {
  protoTxBlockHeader.set_type(txBlockHeader.GetType());
  protoTxBlockHeader.set_version(txBlockHeader.GetVersion());
  NumberToProtobufByteArray<uint256_t, UINT256_SIZE>(
      txBlockHeader.GetGasLimit(), *protoTxBlockHeader.mutable_gaslimit());
  NumberToProtobufByteArray<uint256_t, UINT256_SIZE>(
      txBlockHeader.GetGasUsed(), *protoTxBlockHeader.mutable_gasused());
  protoTxBlockHeader.set_prevhash(txBlockHeader.GetPrevHash().data(),
                                  txBlockHeader.GetPrevHash().size);
  protoTxBlockHeader.set_blocknum(txBlockHeader.GetBlockNum());
  NumberToProtobufByteArray<uint256_t, UINT256_SIZE>(
      txBlockHeader.GetTimestamp(), *protoTxBlockHeader.mutable_timestamp());

  ZilliqaMessage::ProtoTxBlock::TxBlockHashSet* protoHeaderHash =
      protoTxBlockHeader.mutable_hash();
  protoHeaderHash->set_txroothash(txBlockHeader.GetTxRootHash().data(),
                                  txBlockHeader.GetTxRootHash().size);
  protoHeaderHash->set_stateroothash(txBlockHeader.GetStateRootHash().data(),
                                     txBlockHeader.GetStateRootHash().size);
  protoHeaderHash->set_deltaroothash(txBlockHeader.GetDeltaRootHash().data(),
                                     txBlockHeader.GetDeltaRootHash().size);
  protoHeaderHash->set_statedeltahash(txBlockHeader.GetStateDeltaHash().data(),
                                      txBlockHeader.GetStateDeltaHash().size);
  protoHeaderHash->set_tranreceiptroothash(
      txBlockHeader.GetTranReceiptRootHash().data(),
      txBlockHeader.GetTranReceiptRootHash().size);

  protoTxBlockHeader.set_numtxs(txBlockHeader.GetNumTxs());
  protoTxBlockHeader.set_nummicroblockhashes(
      txBlockHeader.GetNumMicroBlockHashes());
  SerializableToProtobufByteArray(txBlockHeader.GetMinerPubKey(),
                                  *protoTxBlockHeader.mutable_minerpubkey());
  protoTxBlockHeader.set_dsblocknum(txBlockHeader.GetDSBlockNum());
  protoTxBlockHeader.set_dsblockheader(txBlockHeader.GetDSBlockHeader().data(),
                                       txBlockHeader.GetDSBlockHeader().size);

  protoTxBlockHeader.set_committeehash(txBlockHeader.GetCommitteeHash().data(),
                                       txBlockHeader.GetCommitteeHash().size);
}

void TxBlockToProtobuf(const TxBlock& txBlock, ProtoTxBlock& protoTxBlock) {
  // Serialize header

  ZilliqaMessage::ProtoTxBlock::TxBlockHeader* protoHeader =
      protoTxBlock.mutable_header();

  const TxBlockHeader& header = txBlock.GetHeader();

  TxBlockHeaderToProtobuf(header, *protoHeader);

  // Serialize body

  for (const auto& i : txBlock.GetIsMicroBlockEmpty()) {
    protoTxBlock.add_ismicroblockempty(i);
  }

  for (const auto& i : txBlock.GetMicroBlockHashes()) {
    ZilliqaMessage::ProtoTxBlock::MicroBlockHashSet* protoHashes =
        protoTxBlock.add_microblockhashes();
    protoHashes->set_txroothash(i.m_txRootHash.data(), i.m_txRootHash.size);
    protoHashes->set_statedeltahash(i.m_stateDeltaHash.data(),
                                    i.m_stateDeltaHash.size);
    protoHashes->set_tranreceipthash(i.m_tranReceiptHash.data(),
                                     i.m_tranReceiptHash.size);
  }

  for (const auto& i : txBlock.GetShardIds()) {
    protoTxBlock.add_shardids(i);
  }

  // Serialize cosigs

  ZilliqaMessage::ProtoTxBlock::CoSignatures* cosigs =
      protoTxBlock.mutable_cosigs();

  SerializableToProtobufByteArray(txBlock.GetCS1(), *cosigs->mutable_cs1());
  for (const auto& i : txBlock.GetB1()) {
    cosigs->add_b1(i);
  }
  SerializableToProtobufByteArray(txBlock.GetCS2(), *cosigs->mutable_cs2());
  for (const auto& i : txBlock.GetB2()) {
    cosigs->add_b2(i);
  }

  // Block hash

  protoTxBlock.set_blockhash(txBlock.GetBlockHash().data(),
                             txBlock.GetBlockHash().size);
}

void ProtobufToTxBlockHeader(
    const ProtoTxBlock::TxBlockHeader& protoTxBlockHeader,
    TxBlockHeader& txBlockHeader) {
  uint256_t gasLimit;
  uint256_t gasUsed;
  BlockHash prevHash;
  uint256_t timestamp;
  TxBlockHashSet hash;
  PubKey minerPubKey;
  BlockHash dsBlockHeader;
  CommitteeHash committeeHash;

  ProtobufByteArrayToNumber<uint256_t, UINT256_SIZE>(
      protoTxBlockHeader.gaslimit(), gasLimit);
  ProtobufByteArrayToNumber<uint256_t, UINT256_SIZE>(
      protoTxBlockHeader.gasused(), gasUsed);
  copy(protoTxBlockHeader.prevhash().begin(),
       protoTxBlockHeader.prevhash().begin() +
           min((unsigned int)protoTxBlockHeader.prevhash().size(),
               (unsigned int)prevHash.size),
       prevHash.asArray().begin());
  ProtobufByteArrayToNumber<uint256_t, UINT256_SIZE>(
      protoTxBlockHeader.timestamp(), timestamp);

  const ZilliqaMessage::ProtoTxBlock::TxBlockHashSet& protoTxBlockHeaderHash =
      protoTxBlockHeader.hash();
  copy(protoTxBlockHeaderHash.txroothash().begin(),
       protoTxBlockHeaderHash.txroothash().begin() +
           min((unsigned int)protoTxBlockHeaderHash.txroothash().size(),
               (unsigned int)hash.m_txRootHash.size),
       hash.m_txRootHash.asArray().begin());
  copy(protoTxBlockHeaderHash.stateroothash().begin(),
       protoTxBlockHeaderHash.stateroothash().begin() +
           min((unsigned int)protoTxBlockHeaderHash.stateroothash().size(),
               (unsigned int)hash.m_stateRootHash.size),
       hash.m_stateRootHash.asArray().begin());
  copy(protoTxBlockHeaderHash.deltaroothash().begin(),
       protoTxBlockHeaderHash.deltaroothash().begin() +
           min((unsigned int)protoTxBlockHeaderHash.deltaroothash().size(),
               (unsigned int)hash.m_deltaRootHash.size),
       hash.m_deltaRootHash.asArray().begin());
  copy(protoTxBlockHeaderHash.statedeltahash().begin(),
       protoTxBlockHeaderHash.statedeltahash().begin() +
           min((unsigned int)protoTxBlockHeaderHash.statedeltahash().size(),
               (unsigned int)hash.m_stateDeltaHash.size),
       hash.m_stateDeltaHash.asArray().begin());
  copy(
      protoTxBlockHeaderHash.tranreceiptroothash().begin(),
      protoTxBlockHeaderHash.tranreceiptroothash().begin() +
          min((unsigned int)protoTxBlockHeaderHash.tranreceiptroothash().size(),
              (unsigned int)hash.m_tranReceiptRootHash.size),
      hash.m_tranReceiptRootHash.asArray().begin());

  ProtobufByteArrayToSerializable(protoTxBlockHeader.minerpubkey(),
                                  minerPubKey);
  copy(protoTxBlockHeader.dsblockheader().begin(),
       protoTxBlockHeader.dsblockheader().begin() +
           min((unsigned int)protoTxBlockHeader.dsblockheader().size(),
               (unsigned int)dsBlockHeader.size),
       dsBlockHeader.asArray().begin());

  copy(protoTxBlockHeader.committeehash().begin(),
       protoTxBlockHeader.committeehash().begin() +
           min((unsigned int)protoTxBlockHeader.committeehash().size(),
               (unsigned int)committeeHash.size),
       committeeHash.asArray().begin());

  txBlockHeader = TxBlockHeader(
      protoTxBlockHeader.type(), protoTxBlockHeader.version(), gasLimit,
      gasUsed, prevHash, protoTxBlockHeader.blocknum(), timestamp,
      hash.m_txRootHash, hash.m_stateRootHash, hash.m_deltaRootHash,
      hash.m_stateDeltaHash, hash.m_tranReceiptRootHash,
      protoTxBlockHeader.numtxs(), protoTxBlockHeader.nummicroblockhashes(),
      minerPubKey, protoTxBlockHeader.dsblocknum(), dsBlockHeader,
      committeeHash);
}

void ProtobufToTxBlock(const ProtoTxBlock& protoTxBlock, TxBlock& txBlock) {
  // Deserialize header

  const ZilliqaMessage::ProtoTxBlock::TxBlockHeader& protoHeader =
      protoTxBlock.header();

  TxBlockHeader header;

  ProtobufToTxBlockHeader(protoHeader, header);

  // Deserialize body

  vector<bool> isMicroBlockEmpty;
  vector<MicroBlockHashSet> microBlockHashes;
  vector<uint32_t> shardIds;

  for (const auto& i : protoTxBlock.ismicroblockempty()) {
    isMicroBlockEmpty.emplace_back(i);
  }

  for (const auto& i : protoTxBlock.microblockhashes()) {
    microBlockHashes.emplace_back();
    copy(i.txroothash().begin(),
         i.txroothash().begin() +
             min((unsigned int)i.txroothash().size(),
                 (unsigned int)microBlockHashes.back().m_txRootHash.size),
         microBlockHashes.back().m_txRootHash.asArray().begin());
    copy(i.statedeltahash().begin(),
         i.statedeltahash().begin() +
             min((unsigned int)i.statedeltahash().size(),
                 (unsigned int)microBlockHashes.back().m_stateDeltaHash.size),
         microBlockHashes.back().m_stateDeltaHash.asArray().begin());
    copy(i.tranreceipthash().begin(),
         i.tranreceipthash().begin() +
             min((unsigned int)i.tranreceipthash().size(),
                 (unsigned int)microBlockHashes.back().m_tranReceiptHash.size),
         microBlockHashes.back().m_tranReceiptHash.asArray().begin());
  }

  for (const auto& i : protoTxBlock.shardids()) {
    shardIds.emplace_back(i);
  }

  // Deserialize cosigs

  CoSignatures cosigs;
  cosigs.m_B1.resize(protoTxBlock.cosigs().b1().size());
  cosigs.m_B2.resize(protoTxBlock.cosigs().b2().size());

  ProtobufByteArrayToSerializable(protoTxBlock.cosigs().cs1(), cosigs.m_CS1);
  copy(protoTxBlock.cosigs().b1().begin(), protoTxBlock.cosigs().b1().end(),
       cosigs.m_B1.begin());
  ProtobufByteArrayToSerializable(protoTxBlock.cosigs().cs2(), cosigs.m_CS2);
  copy(protoTxBlock.cosigs().b2().begin(), protoTxBlock.cosigs().b2().end(),
       cosigs.m_B2.begin());

  // Generate the new TxBlock

  txBlock = TxBlock(header, isMicroBlockEmpty, microBlockHashes, shardIds,
                    CoSignatures(cosigs));

  // Deserialize the block hash
  BlockHash blockHash;

  copy(protoTxBlock.blockhash().begin(),
       protoTxBlock.blockhash().begin() +
           min((unsigned int)protoTxBlock.blockhash().size(),
               (unsigned int)blockHash.size),
       blockHash.asArray().begin());

  txBlock.SetBlockHash(blockHash);
}

void VCBlockHeaderToProtobuf(const VCBlockHeader& vcBlockHeader,
                             ProtoVCBlock::VCBlockHeader& protoVCBlockHeader) {
  protoVCBlockHeader.set_viewchangedsepochno(
      vcBlockHeader.GetVieWChangeDSEpochNo());
  protoVCBlockHeader.set_viewchangeepochno(
      vcBlockHeader.GetViewChangeEpochNo());
  protoVCBlockHeader.set_viewchangestate(vcBlockHeader.GetViewChangeState());
  protoVCBlockHeader.set_candidateleaderindex(
      vcBlockHeader.GetCandidateLeaderIndex());
  SerializableToProtobufByteArray(
      vcBlockHeader.GetCandidateLeaderNetworkInfo(),
      *protoVCBlockHeader.mutable_candidateleadernetworkinfo());
  SerializableToProtobufByteArray(
      vcBlockHeader.GetCandidateLeaderPubKey(),
      *protoVCBlockHeader.mutable_candidateleaderpubkey());
  protoVCBlockHeader.set_vccounter(vcBlockHeader.GetViewChangeCounter());
  NumberToProtobufByteArray<uint256_t, UINT256_SIZE>(
      vcBlockHeader.GetTimeStamp(), *protoVCBlockHeader.mutable_timestamp());

  protoVCBlockHeader.set_committeehash(vcBlockHeader.GetCommitteeHash().data(),
                                       vcBlockHeader.GetCommitteeHash().size);
}

void VCBlockToProtobuf(const VCBlock& vcBlock, ProtoVCBlock& protoVCBlock) {
  // Serialize header

  ZilliqaMessage::ProtoVCBlock::VCBlockHeader* protoHeader =
      protoVCBlock.mutable_header();

  const VCBlockHeader& header = vcBlock.GetHeader();

  VCBlockHeaderToProtobuf(header, *protoHeader);

  // Serialize cosigs

  ZilliqaMessage::ProtoVCBlock::CoSignatures* cosigs =
      protoVCBlock.mutable_cosigs();

  SerializableToProtobufByteArray(vcBlock.GetCS1(), *cosigs->mutable_cs1());
  for (const auto& i : vcBlock.GetB1()) {
    cosigs->add_b1(i);
  }
  SerializableToProtobufByteArray(vcBlock.GetCS2(), *cosigs->mutable_cs2());
  for (const auto& i : vcBlock.GetB2()) {
    cosigs->add_b2(i);
  }

  // Block hash

  protoVCBlock.set_blockhash(vcBlock.GetBlockHash().data(),
                             vcBlock.GetBlockHash().size);
}

void ProtobufToVCBlockHeader(
    const ProtoVCBlock::VCBlockHeader& protoVCBlockHeader,
    VCBlockHeader& vcBlockHeader) {
  Peer candidateLeaderNetworkInfo;
  PubKey candidateLeaderPubKey;
  uint256_t timestamp;
  CommitteeHash committeeHash;

  ProtobufByteArrayToSerializable(
      protoVCBlockHeader.candidateleadernetworkinfo(),
      candidateLeaderNetworkInfo);
  ProtobufByteArrayToSerializable(protoVCBlockHeader.candidateleaderpubkey(),
                                  candidateLeaderPubKey);
  ProtobufByteArrayToNumber<uint256_t, UINT256_SIZE>(
      protoVCBlockHeader.timestamp(), timestamp);

  copy(protoVCBlockHeader.committeehash().begin(),
       protoVCBlockHeader.committeehash().begin() +
           min((unsigned int)protoVCBlockHeader.committeehash().size(),
               (unsigned int)committeeHash.size),
       committeeHash.asArray().begin());

  vcBlockHeader =
      VCBlockHeader(protoVCBlockHeader.viewchangedsepochno(),
                    protoVCBlockHeader.viewchangeepochno(),
                    protoVCBlockHeader.viewchangestate(),
                    protoVCBlockHeader.candidateleaderindex(),
                    candidateLeaderNetworkInfo, candidateLeaderPubKey,
                    protoVCBlockHeader.vccounter(), timestamp, committeeHash);
}

void ProtobufToVCBlock(const ProtoVCBlock& protoVCBlock, VCBlock& vcBlock) {
  // Deserialize header

  const ZilliqaMessage::ProtoVCBlock::VCBlockHeader& protoHeader =
      protoVCBlock.header();

  VCBlockHeader header;

  ProtobufToVCBlockHeader(protoHeader, header);

  // Deserialize cosigs

  CoSignatures cosigs;
  cosigs.m_B1.resize(protoVCBlock.cosigs().b1().size());
  cosigs.m_B2.resize(protoVCBlock.cosigs().b2().size());

  ProtobufByteArrayToSerializable(protoVCBlock.cosigs().cs1(), cosigs.m_CS1);
  copy(protoVCBlock.cosigs().b1().begin(), protoVCBlock.cosigs().b1().end(),
       cosigs.m_B1.begin());
  ProtobufByteArrayToSerializable(protoVCBlock.cosigs().cs2(), cosigs.m_CS2);
  copy(protoVCBlock.cosigs().b2().begin(), protoVCBlock.cosigs().b2().end(),
       cosigs.m_B2.begin());

  // Generate the new VCBlock

  vcBlock = VCBlock(header, CoSignatures(cosigs));

  // Deserialize the block hash
  BlockHash blockHash;

  copy(protoVCBlock.blockhash().begin(),
       protoVCBlock.blockhash().begin() +
           min((unsigned int)protoVCBlock.blockhash().size(),
               (unsigned int)blockHash.size),
       blockHash.asArray().begin());

  vcBlock.SetBlockHash(blockHash);
}

void FallbackBlockHeaderToProtobuf(
    const FallbackBlockHeader& fallbackBlockHeader,
    ProtoFallbackBlock::FallbackBlockHeader& protoFallbackBlockHeader) {
  protoFallbackBlockHeader.set_fallbackdsepochno(
      fallbackBlockHeader.GetFallbackDSEpochNo());
  protoFallbackBlockHeader.set_fallbackepochno(
      fallbackBlockHeader.GetFallbackEpochNo());
  protoFallbackBlockHeader.set_fallbackstate(
      fallbackBlockHeader.GetFallbackState());
  protoFallbackBlockHeader.set_stateroothash(
      fallbackBlockHeader.GetStateRootHash().data(),
      fallbackBlockHeader.GetStateRootHash().size);
  protoFallbackBlockHeader.set_leaderconsensusid(
      fallbackBlockHeader.GetLeaderConsensusId());
  SerializableToProtobufByteArray(
      fallbackBlockHeader.GetLeaderNetworkInfo(),
      *protoFallbackBlockHeader.mutable_leadernetworkinfo());
  SerializableToProtobufByteArray(
      fallbackBlockHeader.GetLeaderPubKey(),
      *protoFallbackBlockHeader.mutable_leaderpubkey());
  protoFallbackBlockHeader.set_shardid(fallbackBlockHeader.GetShardId());
  NumberToProtobufByteArray<uint256_t, UINT256_SIZE>(
      fallbackBlockHeader.GetTimeStamp(),
      *protoFallbackBlockHeader.mutable_timestamp());

  protoFallbackBlockHeader.set_committeehash(
      fallbackBlockHeader.GetCommitteeHash().data(),
      fallbackBlockHeader.GetCommitteeHash().size);
}

void FallbackBlockToProtobuf(const FallbackBlock& fallbackBlock,
                             ProtoFallbackBlock& protoFallbackBlock) {
  // Serialize header

  ZilliqaMessage::ProtoFallbackBlock::FallbackBlockHeader* protoHeader =
      protoFallbackBlock.mutable_header();

  const FallbackBlockHeader& header = fallbackBlock.GetHeader();

  FallbackBlockHeaderToProtobuf(header, *protoHeader);

  // Serialize cosigs

  ZilliqaMessage::ProtoFallbackBlock::CoSignatures* cosigs =
      protoFallbackBlock.mutable_cosigs();

  SerializableToProtobufByteArray(fallbackBlock.GetCS1(),
                                  *cosigs->mutable_cs1());
  for (const auto& i : fallbackBlock.GetB1()) {
    cosigs->add_b1(i);
  }
  SerializableToProtobufByteArray(fallbackBlock.GetCS2(),
                                  *cosigs->mutable_cs2());
  for (const auto& i : fallbackBlock.GetB2()) {
    cosigs->add_b2(i);
  }

  // Block hash

  protoFallbackBlock.set_blockhash(fallbackBlock.GetBlockHash().data(),
                                   fallbackBlock.GetBlockHash().size);
}

void ProtobufToFallbackBlockHeader(
    const ProtoFallbackBlock::FallbackBlockHeader& protoFallbackBlockHeader,
    FallbackBlockHeader& fallbackBlockHeader) {
  Peer leaderNetworkInfo;
  PubKey leaderPubKey;
  uint256_t timestamp;
  StateHash stateRootHash;
  CommitteeHash committeeHash;

  ProtobufByteArrayToSerializable(protoFallbackBlockHeader.leadernetworkinfo(),
                                  leaderNetworkInfo);
  ProtobufByteArrayToSerializable(protoFallbackBlockHeader.leaderpubkey(),
                                  leaderPubKey);
  ProtobufByteArrayToNumber<uint256_t, UINT256_SIZE>(
      protoFallbackBlockHeader.timestamp(), timestamp);

  copy(protoFallbackBlockHeader.stateroothash().begin(),
       protoFallbackBlockHeader.stateroothash().begin() +
           min((unsigned int)protoFallbackBlockHeader.stateroothash().size(),
               (unsigned int)stateRootHash.size),
       stateRootHash.asArray().begin());

  copy(protoFallbackBlockHeader.committeehash().begin(),
       protoFallbackBlockHeader.committeehash().begin() +
           min((unsigned int)protoFallbackBlockHeader.committeehash().size(),
               (unsigned int)committeeHash.size),
       committeeHash.asArray().begin());

  fallbackBlockHeader = FallbackBlockHeader(
      protoFallbackBlockHeader.fallbackdsepochno(),
      protoFallbackBlockHeader.fallbackepochno(),
      protoFallbackBlockHeader.fallbackstate(), stateRootHash,
      protoFallbackBlockHeader.leaderconsensusid(), leaderNetworkInfo,
      leaderPubKey, protoFallbackBlockHeader.shardid(), timestamp,
      committeeHash);
}

void ProtobufToFallbackBlock(const ProtoFallbackBlock& protoFallbackBlock,
                             FallbackBlock& fallbackBlock) {
  // Deserialize header
  const ZilliqaMessage::ProtoFallbackBlock::FallbackBlockHeader& protoHeader =
      protoFallbackBlock.header();

  FallbackBlockHeader header;

  ProtobufToFallbackBlockHeader(protoHeader, header);

  // Deserialize cosigs

  CoSignatures cosigs;
  cosigs.m_B1.resize(protoFallbackBlock.cosigs().b1().size());
  cosigs.m_B2.resize(protoFallbackBlock.cosigs().b2().size());

  ProtobufByteArrayToSerializable(protoFallbackBlock.cosigs().cs1(),
                                  cosigs.m_CS1);
  copy(protoFallbackBlock.cosigs().b1().begin(),
       protoFallbackBlock.cosigs().b1().end(), cosigs.m_B1.begin());
  ProtobufByteArrayToSerializable(protoFallbackBlock.cosigs().cs2(),
                                  cosigs.m_CS2);
  copy(protoFallbackBlock.cosigs().b2().begin(),
       protoFallbackBlock.cosigs().b2().end(), cosigs.m_B2.begin());

  // Generate the new FallbackBlock
  fallbackBlock = FallbackBlock(header, CoSignatures(cosigs));

  // Deserialize the block hash
  BlockHash blockHash;

  copy(protoFallbackBlock.blockhash().begin(),
       protoFallbackBlock.blockhash().begin() +
           min((unsigned int)protoFallbackBlock.blockhash().size(),
               (unsigned int)blockHash.size),
       blockHash.asArray().begin());

  fallbackBlock.SetBlockHash(blockHash);
}

template <class T>
bool SerializeToArray(const T& protoMessage, vector<unsigned char>& dst,
                      const unsigned int offset) {
  const int length_available = dst.size() - offset;

  if (length_available < protoMessage.ByteSize()) {
    dst.resize(dst.size() + protoMessage.ByteSize() - length_available);
  }

  return protoMessage.SerializeToArray(dst.data() + offset,
                                       protoMessage.ByteSize());
}

template <class T>
bool RepeatableToArray(const T& repeatable, vector<unsigned char>& dst,
                       const unsigned int offset) {
  int tempOffset = offset;
  for (const auto& element : repeatable) {
    if (!SerializeToArray(element, dst, tempOffset)) {
      LOG_GENERAL(WARNING, "SerializeToArray failed, offset: " << tempOffset);
      return false;
    }
    tempOffset += element.ByteSize();
  }
  return true;
}

template <class T, size_t S>
void NumberToArray(const T& number, vector<unsigned char>& dst,
                   const unsigned int offset) {
  Serializable::SetNumber<T>(dst, offset, number, S);
}

bool SetConsensusAnnouncementCore(
    ZilliqaMessage::ConsensusAnnouncement& announcement,
    const uint32_t consensusID, uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const pair<PrivKey, PubKey>& leaderKey) {
  LOG_MARKER();

  // Set the consensus parameters

  announcement.mutable_consensusinfo()->set_consensusid(consensusID);
  announcement.mutable_consensusinfo()->set_blocknumber(blockNumber);
  announcement.mutable_consensusinfo()->set_blockhash(blockHash.data(),
                                                      blockHash.size());
  announcement.mutable_consensusinfo()->set_leaderid(leaderID);

  if (!announcement.consensusinfo().IsInitialized()) {
    LOG_GENERAL(WARNING,
                "ConsensusAnnouncement.ConsensusInfo initialization failed.");
    return false;
  }

  // Sign the announcement

  vector<unsigned char> inputToSigning;

  switch (announcement.announcement_case()) {
    case ConsensusAnnouncement::AnnouncementCase::kDsblock:
      if (!announcement.dsblock().IsInitialized()) {
        LOG_GENERAL(WARNING, "Announcement dsblock content not initialized.");
        return false;
      }
      inputToSigning.resize(announcement.consensusinfo().ByteSize() +
                            announcement.dsblock().ByteSize());
      announcement.consensusinfo().SerializeToArray(
          inputToSigning.data(), announcement.consensusinfo().ByteSize());
      announcement.dsblock().SerializeToArray(
          inputToSigning.data() + announcement.consensusinfo().ByteSize(),
          announcement.dsblock().ByteSize());
      break;
    case ConsensusAnnouncement::AnnouncementCase::kMicroblock:
      if (!announcement.microblock().IsInitialized()) {
        LOG_GENERAL(WARNING,
                    "Announcement microblock content not initialized.");
        return false;
      }
      inputToSigning.resize(announcement.consensusinfo().ByteSize() +
                            announcement.microblock().ByteSize());
      announcement.consensusinfo().SerializeToArray(
          inputToSigning.data(), announcement.consensusinfo().ByteSize());
      announcement.microblock().SerializeToArray(
          inputToSigning.data() + announcement.consensusinfo().ByteSize(),
          announcement.microblock().ByteSize());
      break;
    case ConsensusAnnouncement::AnnouncementCase::kFinalblock:
      if (!announcement.finalblock().IsInitialized()) {
        LOG_GENERAL(WARNING,
                    "Announcement finalblock content not initialized.");
        return false;
      }
      inputToSigning.resize(announcement.consensusinfo().ByteSize() +
                            announcement.finalblock().ByteSize());
      announcement.consensusinfo().SerializeToArray(
          inputToSigning.data(), announcement.consensusinfo().ByteSize());
      announcement.finalblock().SerializeToArray(
          inputToSigning.data() + announcement.consensusinfo().ByteSize(),
          announcement.finalblock().ByteSize());
      break;
    case ConsensusAnnouncement::AnnouncementCase::kVcblock:
      if (!announcement.vcblock().IsInitialized()) {
        LOG_GENERAL(WARNING, "Announcement vcblock content not initialized.");
        return false;
      }
      inputToSigning.resize(announcement.consensusinfo().ByteSize() +
                            announcement.vcblock().ByteSize());
      announcement.consensusinfo().SerializeToArray(
          inputToSigning.data(), announcement.consensusinfo().ByteSize());
      announcement.vcblock().SerializeToArray(
          inputToSigning.data() + announcement.consensusinfo().ByteSize(),
          announcement.vcblock().ByteSize());
      break;
    case ConsensusAnnouncement::AnnouncementCase::kFallbackblock:
      if (!announcement.fallbackblock().IsInitialized()) {
        LOG_GENERAL(WARNING,
                    "Announcement fallbackblock content not initialized.");
        return false;
      }
      inputToSigning.resize(announcement.consensusinfo().ByteSize() +
                            announcement.fallbackblock().ByteSize());
      announcement.consensusinfo().SerializeToArray(
          inputToSigning.data(), announcement.consensusinfo().ByteSize());
      announcement.fallbackblock().SerializeToArray(
          inputToSigning.data() + announcement.consensusinfo().ByteSize(),
          announcement.fallbackblock().ByteSize());
      break;
    case ConsensusAnnouncement::AnnouncementCase::ANNOUNCEMENT_NOT_SET:
    default:
      LOG_GENERAL(WARNING, "Announcement content not set.");
      return false;
  }

  Signature signature;
  if (!Schnorr::GetInstance().Sign(inputToSigning, leaderKey.first,
                                   leaderKey.second, signature)) {
    LOG_GENERAL(WARNING, "Failed to sign announcement.");
    return false;
  }

  SerializableToProtobufByteArray(signature, *announcement.mutable_signature());

  return announcement.IsInitialized();
}

bool GetConsensusAnnouncementCore(
    const ZilliqaMessage::ConsensusAnnouncement& announcement,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const PubKey& leaderKey) {
  LOG_MARKER();

  // Check the consensus parameters

  if (announcement.consensusinfo().consensusid() != consensusID) {
    LOG_GENERAL(WARNING, "Consensus ID mismatch. Expected: "
                             << consensusID << " Actual: "
                             << announcement.consensusinfo().consensusid());
    return false;
  }

  if (announcement.consensusinfo().blocknumber() != blockNumber) {
    LOG_GENERAL(WARNING, "Block number mismatch. Expected: "
                             << blockNumber << " Actual: "
                             << announcement.consensusinfo().blocknumber());
    return false;
  }

  if ((announcement.consensusinfo().blockhash().size() != blockHash.size()) ||
      !equal(blockHash.begin(), blockHash.end(),
             announcement.consensusinfo().blockhash().begin(),
             [](const unsigned char left, const char right) -> bool {
               return left == (unsigned char)right;
             })) {
    std::vector<unsigned char> remoteBlockHash;
    remoteBlockHash.resize(announcement.consensusinfo().blockhash().size());
    std::copy(announcement.consensusinfo().blockhash().begin(),
              announcement.consensusinfo().blockhash().end(),
              remoteBlockHash.begin());
    LOG_GENERAL(WARNING,
                "Block hash mismatch. Expected: "
                    << DataConversion::Uint8VecToHexStr(blockHash)
                    << " Actual: "
                    << DataConversion::Uint8VecToHexStr(remoteBlockHash));
    return false;
  }

  if (announcement.consensusinfo().leaderid() != leaderID) {
    LOG_GENERAL(WARNING, "Leader ID mismatch. Expected: "
                             << leaderID << " Actual: "
                             << announcement.consensusinfo().leaderid());
    return false;
  }

  // Verify the signature

  vector<unsigned char> tmp;

  if (announcement.has_dsblock() && announcement.dsblock().IsInitialized()) {
    tmp.resize(announcement.consensusinfo().ByteSize() +
               announcement.dsblock().ByteSize());
    announcement.consensusinfo().SerializeToArray(
        tmp.data(), announcement.consensusinfo().ByteSize());
    announcement.dsblock().SerializeToArray(
        tmp.data() + announcement.consensusinfo().ByteSize(),
        announcement.dsblock().ByteSize());
  } else if (announcement.has_microblock() &&
             announcement.microblock().IsInitialized()) {
    tmp.resize(announcement.consensusinfo().ByteSize() +
               announcement.microblock().ByteSize());
    announcement.consensusinfo().SerializeToArray(
        tmp.data(), announcement.consensusinfo().ByteSize());
    announcement.microblock().SerializeToArray(
        tmp.data() + announcement.consensusinfo().ByteSize(),
        announcement.microblock().ByteSize());
  } else if (announcement.has_finalblock() &&
             announcement.finalblock().IsInitialized()) {
    tmp.resize(announcement.consensusinfo().ByteSize() +
               announcement.finalblock().ByteSize());
    announcement.consensusinfo().SerializeToArray(
        tmp.data(), announcement.consensusinfo().ByteSize());
    announcement.finalblock().SerializeToArray(
        tmp.data() + announcement.consensusinfo().ByteSize(),
        announcement.finalblock().ByteSize());
  } else if (announcement.has_vcblock() &&
             announcement.vcblock().IsInitialized()) {
    tmp.resize(announcement.consensusinfo().ByteSize() +
               announcement.vcblock().ByteSize());
    announcement.consensusinfo().SerializeToArray(
        tmp.data(), announcement.consensusinfo().ByteSize());
    announcement.vcblock().SerializeToArray(
        tmp.data() + announcement.consensusinfo().ByteSize(),
        announcement.vcblock().ByteSize());
  } else if (announcement.has_fallbackblock() &&
             announcement.fallbackblock().IsInitialized()) {
    tmp.resize(announcement.consensusinfo().ByteSize() +
               announcement.fallbackblock().ByteSize());
    announcement.consensusinfo().SerializeToArray(
        tmp.data(), announcement.consensusinfo().ByteSize());
    announcement.fallbackblock().SerializeToArray(
        tmp.data() + announcement.consensusinfo().ByteSize(),
        announcement.fallbackblock().ByteSize());
  } else {
    LOG_GENERAL(WARNING, "Announcement content not set.");
    return false;
  }

  Signature signature;

  ProtobufByteArrayToSerializable(announcement.signature(), signature);

  if (!Schnorr::GetInstance().Verify(tmp, signature, leaderKey)) {
    LOG_GENERAL(WARNING, "Invalid signature in announcement.");
    return false;
  }

  return true;
}
}  // namespace

// ============================================================================
// Primitives
// ============================================================================

bool Messenger::GetDSCommitteeHash(const deque<pair<PubKey, Peer>>& dsCommittee,
                                   CommitteeHash& dst) {
  ProtoCommittee protoCommittee;

  DSCommitteeToProtoCommittee(dsCommittee, protoCommittee);

  if (!protoCommittee.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoCommittee initialization failed.");
    return false;
  }

  vector<unsigned char> tmp;

  if (!SerializeToArray(protoCommittee, tmp, 0)) {
    LOG_GENERAL(WARNING, "ProtoCommittee serialization failed.");
    return false;
  }

  SHA2<HASH_TYPE::HASH_VARIANT_256> sha2;
  sha2.Update(tmp);
  tmp = sha2.Finalize();

  copy(tmp.begin(), tmp.end(), dst.asArray().begin());

  return true;
}

bool Messenger::GetShardHash(const Shard& shard, CommitteeHash& dst) {
  ProtoCommittee protoCommittee;

  ShardToProtoCommittee(shard, protoCommittee);

  if (!protoCommittee.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoCommittee initialization failed.");
    return false;
  }

  vector<unsigned char> tmp;

  if (!SerializeToArray(protoCommittee, tmp, 0)) {
    LOG_GENERAL(WARNING, "ProtoCommittee serialization failed.");
    return false;
  }

  SHA2<HASH_TYPE::HASH_VARIANT_256> sha2;
  sha2.Update(tmp);
  tmp = sha2.Finalize();

  copy(tmp.begin(), tmp.end(), dst.asArray().begin());

  return true;
}

bool Messenger::GetShardingStructureHash(const DequeOfShard& shards,
                                         ShardingHash& dst) {
  ProtoShardingStructure protoShardingStructure;

  ShardingStructureToProtobuf(shards, protoShardingStructure);

  if (!protoShardingStructure.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoShardingStructure initialization failed.");
    return false;
  }

  vector<unsigned char> tmp;

  if (!SerializeToArray(protoShardingStructure, tmp, 0)) {
    LOG_GENERAL(WARNING, "ProtoShardingStructure serialization failed.");
    return false;
  }

  SHA2<HASH_TYPE::HASH_VARIANT_256> sha2;
  sha2.Update(tmp);
  tmp = sha2.Finalize();

  copy(tmp.begin(), tmp.end(), dst.asArray().begin());

  return true;
}

bool Messenger::GetTxSharingAssignmentsHash(
    const vector<Peer>& dsReceivers, const vector<vector<Peer>>& shardReceivers,
    const vector<vector<Peer>>& shardSenders, TxSharingHash& dst) {
  ProtoTxSharingAssignments protoTxSharingAssignments;

  TxSharingAssignmentsToProtobuf(dsReceivers, shardReceivers, shardSenders,
                                 protoTxSharingAssignments);

  if (!protoTxSharingAssignments.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoTxSharingAssignments initialization failed.");
    return false;
  }

  vector<unsigned char> tmp;

  if (!SerializeToArray(protoTxSharingAssignments, tmp, 0)) {
    LOG_GENERAL(WARNING, "ProtoTxSharingAssignments serialization failed.");
    return false;
  }

  SHA2<HASH_TYPE::HASH_VARIANT_256> sha2;
  sha2.Update(tmp);
  tmp = sha2.Finalize();

  copy(tmp.begin(), tmp.end(), dst.asArray().begin());

  return true;
}

bool Messenger::SetDSBlockHeader(vector<unsigned char>& dst,
                                 const unsigned int offset,
                                 const DSBlockHeader& dsBlockHeader) {
  ProtoDSBlock::DSBlockHeader result;

  DSBlockHeaderToProtobuf(dsBlockHeader, result);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoDSBlock::DSBlockHeader initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetDSBlockHeader(const vector<unsigned char>& src,
                                 const unsigned int offset,
                                 DSBlockHeader& dsBlockHeader) {
  ProtoDSBlock::DSBlockHeader result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoDSBlock::DSBlockHeader initialization failed.");
    return false;
  }

  ProtobufToDSBlockHeader(result, dsBlockHeader);

  return true;
}

bool Messenger::SetDSBlock(vector<unsigned char>& dst,
                           const unsigned int offset, const DSBlock& dsBlock) {
  ProtoDSBlock result;

  DSBlockToProtobuf(dsBlock, result);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoDSBlock initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetDSBlock(const vector<unsigned char>& src,
                           const unsigned int offset, DSBlock& dsBlock) {
  ProtoDSBlock result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoDSBlock initialization failed.");
    return false;
  }

  ProtobufToDSBlock(result, dsBlock);

  return true;
}

bool Messenger::SetMicroBlockHeader(vector<unsigned char>& dst,
                                    const unsigned int offset,
                                    const MicroBlockHeader& microBlockHeader) {
  ProtoMicroBlock::MicroBlockHeader result;

  MicroBlockHeaderToProtobuf(microBlockHeader, result);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING,
                "ProtoMicroBlock::MicroBlockHeader initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetMicroBlockHeader(const vector<unsigned char>& src,
                                    const unsigned int offset,
                                    MicroBlockHeader& microBlockHeader) {
  ProtoMicroBlock::MicroBlockHeader result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING,
                "ProtoMicroBlock::MicroBlockHeader initialization failed.");
    return false;
  }

  ProtobufToMicroBlockHeader(result, microBlockHeader);

  return true;
}

bool Messenger::SetMicroBlock(vector<unsigned char>& dst,
                              const unsigned int offset,
                              const MicroBlock& microBlock) {
  ProtoMicroBlock result;

  MicroBlockToProtobuf(microBlock, result);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoMicroBlock initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetMicroBlock(const vector<unsigned char>& src,
                              const unsigned int offset,
                              MicroBlock& microBlock) {
  ProtoMicroBlock result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoMicroBlock initialization failed.");
    return false;
  }

  ProtobufToMicroBlock(result, microBlock);

  return true;
}

bool Messenger::SetTxBlockHeader(vector<unsigned char>& dst,
                                 const unsigned int offset,
                                 const TxBlockHeader& txBlockHeader) {
  ProtoTxBlock::TxBlockHeader result;

  TxBlockHeaderToProtobuf(txBlockHeader, result);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoTxBlock::TxBlockHeader initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetTxBlockHeader(const vector<unsigned char>& src,
                                 const unsigned int offset,
                                 TxBlockHeader& txBlockHeader) {
  ProtoTxBlock::TxBlockHeader result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoTxBlock::TxBlockHeader initialization failed.");
    return false;
  }

  ProtobufToTxBlockHeader(result, txBlockHeader);

  return true;
}

bool Messenger::SetTxBlock(vector<unsigned char>& dst,
                           const unsigned int offset, const TxBlock& txBlock) {
  ProtoTxBlock result;

  TxBlockToProtobuf(txBlock, result);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoTxBlock initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetTxBlock(const vector<unsigned char>& src,
                           const unsigned int offset, TxBlock& txBlock) {
  ProtoTxBlock result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoTxBlock initialization failed.");
    return false;
  }

  ProtobufToTxBlock(result, txBlock);

  return true;
}

bool Messenger::SetVCBlockHeader(vector<unsigned char>& dst,
                                 const unsigned int offset,
                                 const VCBlockHeader& vcBlockHeader) {
  ProtoVCBlock::VCBlockHeader result;

  VCBlockHeaderToProtobuf(vcBlockHeader, result);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoVCBlock::VCBlockHeader initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetVCBlockHeader(const vector<unsigned char>& src,
                                 const unsigned int offset,
                                 VCBlockHeader& vcBlockHeader) {
  ProtoVCBlock::VCBlockHeader result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoVCBlock::VCBlockHeader initialization failed.");
    return false;
  }

  ProtobufToVCBlockHeader(result, vcBlockHeader);

  return true;
}

bool Messenger::SetVCBlock(vector<unsigned char>& dst,
                           const unsigned int offset, const VCBlock& vcBlock) {
  ProtoVCBlock result;

  VCBlockToProtobuf(vcBlock, result);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoVCBlock initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetVCBlock(const vector<unsigned char>& src,
                           const unsigned int offset, VCBlock& vcBlock) {
  ProtoVCBlock result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoVCBlock initialization failed.");
    return false;
  }

  ProtobufToVCBlock(result, vcBlock);

  return true;
}

bool Messenger::SetFallbackBlockHeader(
    vector<unsigned char>& dst, const unsigned int offset,
    const FallbackBlockHeader& fallbackBlockHeader) {
  ProtoFallbackBlock::FallbackBlockHeader result;

  FallbackBlockHeaderToProtobuf(fallbackBlockHeader, result);

  if (!result.IsInitialized()) {
    LOG_GENERAL(
        WARNING,
        "ProtoFallbackBlock::FallbackBlockHeader initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetFallbackBlockHeader(
    const vector<unsigned char>& src, const unsigned int offset,
    FallbackBlockHeader& fallbackBlockHeader) {
  ProtoFallbackBlock::FallbackBlockHeader result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(
        WARNING,
        "ProtoFallbackBlock::FallbackBlockHeader initialization failed.");
    return false;
  }

  ProtobufToFallbackBlockHeader(result, fallbackBlockHeader);

  return true;
}

bool Messenger::SetFallbackBlock(vector<unsigned char>& dst,
                                 const unsigned int offset,
                                 const FallbackBlock& fallbackBlock) {
  ProtoFallbackBlock result;

  FallbackBlockToProtobuf(fallbackBlock, result);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoFallbackBlock initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetFallbackBlock(const vector<unsigned char>& src,
                                 const unsigned int offset,
                                 FallbackBlock& fallbackBlock) {
  ProtoFallbackBlock result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoFallbackBlock initialization failed.");
    return false;
  }

  ProtobufToFallbackBlock(result, fallbackBlock);

  return true;
}

// ============================================================================
// Directory Service messages
// ============================================================================

bool Messenger::SetDSPoWSubmission(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint64_t blockNumber, const uint8_t difficultyLevel,
    const Peer& submitterPeer, const pair<PrivKey, PubKey>& submitterKey,
    const uint64_t nonce, const string& resultingHash, const string& mixHash) {
  LOG_MARKER();

  DSPoWSubmission result;

  result.mutable_data()->set_blocknumber(blockNumber);
  result.mutable_data()->set_difficultylevel(difficultyLevel);

  SerializableToProtobufByteArray(
      submitterPeer, *result.mutable_data()->mutable_submitterpeer());
  SerializableToProtobufByteArray(
      submitterKey.second, *result.mutable_data()->mutable_submitterpubkey());

  result.mutable_data()->set_nonce(nonce);
  result.mutable_data()->set_resultinghash(resultingHash);
  result.mutable_data()->set_mixhash(mixHash);

  if (result.data().IsInitialized()) {
    vector<unsigned char> tmp(result.data().ByteSize());
    result.data().SerializeToArray(tmp.data(), tmp.size());

    Signature signature;
    if (!Schnorr::GetInstance().Sign(tmp, submitterKey.first,
                                     submitterKey.second, signature)) {
      LOG_GENERAL(WARNING, "Failed to sign PoW.");
      return false;
    }

    SerializableToProtobufByteArray(signature, *result.mutable_signature());
  } else {
    LOG_GENERAL(WARNING, "DSPoWSubmission.Data initialization failed.");
    return false;
  }

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "DSPoWSubmission initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetDSPoWSubmission(const vector<unsigned char>& src,
                                   const unsigned int offset,
                                   uint64_t& blockNumber,
                                   uint8_t& difficultyLevel,
                                   Peer& submitterPeer, PubKey& submitterPubKey,
                                   uint64_t& nonce, string& resultingHash,
                                   string& mixHash, Signature& signature) {
  LOG_MARKER();

  DSPoWSubmission result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized() || !result.data().IsInitialized()) {
    LOG_GENERAL(WARNING, "DSPoWSubmission initialization failed.");
    return false;
  }

  blockNumber = result.data().blocknumber();
  difficultyLevel = result.data().difficultylevel();
  ProtobufByteArrayToSerializable(result.data().submitterpeer(), submitterPeer);
  ProtobufByteArrayToSerializable(result.data().submitterpubkey(),
                                  submitterPubKey);
  nonce = result.data().nonce();
  resultingHash = result.data().resultinghash();
  mixHash = result.data().mixhash();
  ProtobufByteArrayToSerializable(result.signature(), signature);

  vector<unsigned char> tmp(result.data().ByteSize());
  result.data().SerializeToArray(tmp.data(), tmp.size());

  if (!Schnorr::GetInstance().Verify(tmp, 0, tmp.size(), signature,
                                     submitterPubKey)) {
    LOG_GENERAL(WARNING, "PoW submission signature wrong.");
    return false;
  }

  return true;
}

bool Messenger::SetDSMicroBlockSubmission(
    vector<unsigned char>& dst, const unsigned int offset,
    const unsigned char microBlockType, const uint64_t blockNumber,
    const vector<MicroBlock>& microBlocks,
    const vector<unsigned char>& stateDelta) {
  LOG_MARKER();

  DSMicroBlockSubmission result;

  result.set_microblocktype(microBlockType);
  result.set_blocknumber(blockNumber);
  for (const auto& microBlock : microBlocks) {
    MicroBlockToProtobuf(microBlock, *result.add_microblocks());
  }
  if (stateDelta.size() > 0) {
    result.set_statedelta(stateDelta.data(), stateDelta.size());
  }

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "DSMicroBlockSubmission initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetDSMicroBlockSubmission(const vector<unsigned char>& src,
                                          const unsigned int offset,
                                          unsigned char& microBlockType,
                                          uint64_t& blockNumber,
                                          vector<MicroBlock>& microBlocks,
                                          vector<unsigned char>& stateDelta) {
  LOG_MARKER();

  DSMicroBlockSubmission result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "DSMicroBlockSubmission initialization failed.");
    return false;
  }

  microBlockType = result.microblocktype();
  blockNumber = result.blocknumber();
  for (const auto& proto_mb : result.microblocks()) {
    MicroBlock microBlock;
    ProtobufToMicroBlock(proto_mb, microBlock);
    microBlocks.emplace_back(move(microBlock));
  }
  if (result.has_statedelta()) {
    stateDelta.resize(result.statedelta().size());
    copy(result.statedelta().begin(), result.statedelta().end(),
         stateDelta.begin());
  }

  return true;
}

bool Messenger::SetDSDSBlockAnnouncement(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const pair<PrivKey, PubKey>& leaderKey, const DSBlock& dsBlock,
    const DequeOfShard& shards, const vector<Peer>& dsReceivers,
    const vector<vector<Peer>>& shardReceivers,
    const vector<vector<Peer>>& shardSenders,
    vector<unsigned char>& messageToCosign) {
  LOG_MARKER();

  ConsensusAnnouncement announcement;

  // Set the DSBlock announcement parameters

  DSDSBlockAnnouncement* dsblock = announcement.mutable_dsblock();

  DSBlockToProtobuf(dsBlock, *dsblock->mutable_dsblock());

  ShardingStructureToProtobuf(shards, *dsblock->mutable_sharding());

  TxSharingAssignmentsToProtobuf(dsReceivers, shardReceivers, shardSenders,
                                 *dsblock->mutable_assignments());

  if (!dsblock->IsInitialized()) {
    LOG_GENERAL(WARNING, "DSDSBlockAnnouncement initialization failed. Debug: "
                             << announcement.DebugString());
    return false;
  }

  // Set the common consensus announcement parameters

  if (!SetConsensusAnnouncementCore(announcement, consensusID, blockNumber,
                                    blockHash, leaderID, leaderKey)) {
    LOG_GENERAL(WARNING, "SetConsensusAnnouncementCore failed. Debug: "
                             << announcement.DebugString());
    return false;
  }

  // Serialize the part of the announcement that should be co-signed during the
  // first round of consensus

  messageToCosign.clear();
  if (!dsBlock.GetHeader().Serialize(messageToCosign, 0)) {
    LOG_GENERAL(WARNING, "DSBlockHeader serialization failed.");
    return false;
  }

  // Serialize the announcement

  return SerializeToArray(announcement, dst, offset);
}

bool Messenger::GetDSDSBlockAnnouncement(
    const vector<unsigned char>& src, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const PubKey& leaderKey, DSBlock& dsBlock, DequeOfShard& shards,
    vector<Peer>& dsReceivers, vector<vector<Peer>>& shardReceivers,
    vector<vector<Peer>>& shardSenders,
    vector<unsigned char>& messageToCosign) {
  LOG_MARKER();

  ConsensusAnnouncement announcement;

  announcement.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!announcement.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusAnnouncement initialization failed. Debug: "
                             << announcement.DebugString());
    return false;
  }

  if (!announcement.has_dsblock()) {
    LOG_GENERAL(
        WARNING,
        "DSDSBlockAnnouncement initialization failed (no ds block). Debug: "
            << announcement.DebugString());
    return false;
  }

  // Check the common consensus announcement parameters

  if (!GetConsensusAnnouncementCore(announcement, consensusID, blockNumber,
                                    blockHash, leaderID, leaderKey)) {
    LOG_GENERAL(WARNING, "GetConsensusAnnouncementCore failed.");
    return false;
  }

  // Get the DSBlock announcement parameters

  const DSDSBlockAnnouncement& dsblock = announcement.dsblock();

  ProtobufToDSBlock(dsblock.dsblock(), dsBlock);

  ProtobufToShardingStructure(dsblock.sharding(), shards);

  ProtobufToTxSharingAssignments(dsblock.assignments(), dsReceivers,
                                 shardReceivers, shardSenders);

  // Get the part of the announcement that should be co-signed during the first
  // round of consensus

  messageToCosign.clear();
  if (!dsBlock.GetHeader().Serialize(messageToCosign, 0)) {
    LOG_GENERAL(WARNING, "DSBlockHeader serialization failed.");
    return false;
  }

  return true;
}

bool Messenger::SetDSFinalBlockAnnouncement(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const pair<PrivKey, PubKey>& leaderKey, const TxBlock& txBlock,
    vector<unsigned char>& messageToCosign) {
  LOG_MARKER();

  ConsensusAnnouncement announcement;

  // Set the FinalBlock announcement parameters

  DSFinalBlockAnnouncement* finalblock = announcement.mutable_finalblock();
  TxBlockToProtobuf(txBlock, *finalblock->mutable_txblock());

  if (!finalblock->IsInitialized()) {
    LOG_GENERAL(WARNING, "DSFinalBlockAnnouncement initialization failed.");
    return false;
  }

  // Set the common consensus announcement parameters

  if (!SetConsensusAnnouncementCore(announcement, consensusID, blockNumber,
                                    blockHash, leaderID, leaderKey)) {
    LOG_GENERAL(WARNING, "SetConsensusAnnouncementCore failed.");
    return false;
  }

  // Serialize the part of the announcement that should be co-signed during the
  // first round of consensus

  messageToCosign.clear();
  if (!txBlock.GetHeader().Serialize(messageToCosign, 0)) {
    LOG_GENERAL(WARNING, "TxBlockHeader serialization failed.");
    return false;
  }

  // Serialize the announcement

  return SerializeToArray(announcement, dst, offset);
}

bool Messenger::GetDSFinalBlockAnnouncement(
    const vector<unsigned char>& src, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const PubKey& leaderKey, TxBlock& txBlock,
    vector<unsigned char>& messageToCosign) {
  LOG_MARKER();

  ConsensusAnnouncement announcement;

  announcement.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!announcement.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusAnnouncement initialization failed.");
    return false;
  }

  if (!announcement.has_finalblock()) {
    LOG_GENERAL(WARNING, "DSFinalBlockAnnouncement initialization failed.");
    return false;
  }

  // Check the common consensus announcement parameters

  if (!GetConsensusAnnouncementCore(announcement, consensusID, blockNumber,
                                    blockHash, leaderID, leaderKey)) {
    LOG_GENERAL(WARNING, "GetConsensusAnnouncementCore failed.");
    return false;
  }

  // Get the FinalBlock announcement parameters

  const DSFinalBlockAnnouncement& finalblock = announcement.finalblock();
  ProtobufToTxBlock(finalblock.txblock(), txBlock);

  // Get the part of the announcement that should be co-signed during the first
  // round of consensus

  messageToCosign.clear();
  if (!txBlock.GetHeader().Serialize(messageToCosign, 0)) {
    LOG_GENERAL(WARNING, "TxBlockHeader serialization failed.");
    return false;
  }

  return true;
}

bool Messenger::SetDSVCBlockAnnouncement(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const pair<PrivKey, PubKey>& leaderKey, const VCBlock& vcBlock,
    vector<unsigned char>& messageToCosign) {
  LOG_MARKER();

  ConsensusAnnouncement announcement;

  // Set the VCBlock announcement parameters

  DSVCBlockAnnouncement* vcblock = announcement.mutable_vcblock();
  SerializableToProtobufByteArray(vcBlock, *vcblock->mutable_vcblock());

  if (!vcblock->IsInitialized()) {
    LOG_GENERAL(WARNING, "DSVCBlockAnnouncement initialization failed.");
    return false;
  }

  // Set the common consensus announcement parameters

  if (!SetConsensusAnnouncementCore(announcement, consensusID, blockNumber,
                                    blockHash, leaderID, leaderKey)) {
    LOG_GENERAL(WARNING, "SetConsensusAnnouncementCore failed.");
    return false;
  }

  // Serialize the part of the announcement that should be co-signed during the
  // first round of consensus

  messageToCosign.clear();
  if (!vcBlock.GetHeader().Serialize(messageToCosign, 0)) {
    LOG_GENERAL(WARNING, "VCBlockHeader serialization failed.");
    return false;
  }

  // Serialize the announcement

  return SerializeToArray(announcement, dst, offset);
}

bool Messenger::GetDSVCBlockAnnouncement(
    const vector<unsigned char>& src, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const PubKey& leaderKey, VCBlock& vcBlock,
    vector<unsigned char>& messageToCosign) {
  LOG_MARKER();

  ConsensusAnnouncement announcement;

  announcement.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!announcement.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusAnnouncement initialization failed.");
    return false;
  }

  if (!announcement.has_vcblock()) {
    LOG_GENERAL(WARNING, "DSVCBlockAnnouncement initialization failed.");
    return false;
  }

  // Check the common consensus announcement parameters

  if (!GetConsensusAnnouncementCore(announcement, consensusID, blockNumber,
                                    blockHash, leaderID, leaderKey)) {
    LOG_GENERAL(WARNING, "GetConsensusAnnouncementCore failed.");
    return false;
  }

  // Get the VCBlock announcement parameters

  const DSVCBlockAnnouncement& vcblock = announcement.vcblock();
  ProtobufByteArrayToSerializable(vcblock.vcblock(), vcBlock);

  // Get the part of the announcement that should be co-signed during the first
  // round of consensus

  messageToCosign.clear();
  if (!vcBlock.GetHeader().Serialize(messageToCosign, 0)) {
    LOG_GENERAL(WARNING, "VCBlockHeader serialization failed.");
    return false;
  }

  return true;
}

// ============================================================================
// Node messages
// ============================================================================

bool Messenger::SetNodeVCDSBlocksMessage(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint32_t shardId, const DSBlock& dsBlock,
    const std::vector<VCBlock>& vcBlocks, const DequeOfShard& shards,
    const vector<Peer>& dsReceivers, const vector<vector<Peer>>& shardReceivers,
    const vector<vector<Peer>>& shardSenders) {
  LOG_MARKER();

  NodeDSBlock result;

  result.set_shardid(shardId);
  DSBlockToProtobuf(dsBlock, *result.mutable_dsblock());

  for (const auto& vcblock : vcBlocks) {
    VCBlockToProtobuf(vcblock, *result.add_vcblocks());
  }
  ShardingStructureToProtobuf(shards, *result.mutable_sharding());

  TxSharingAssignmentsToProtobuf(dsReceivers, shardReceivers, shardSenders,
                                 *result.mutable_assignments());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeDSBlock initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetNodeVCDSBlocksMessage(
    const vector<unsigned char>& src, const unsigned int offset,
    uint32_t& shardId, DSBlock& dsBlock, std::vector<VCBlock>& vcBlocks,
    DequeOfShard& shards, vector<Peer>& dsReceivers,
    vector<vector<Peer>>& shardReceivers, vector<vector<Peer>>& shardSenders) {
  LOG_MARKER();

  NodeDSBlock result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeDSBlock initialization failed.");
    return false;
  }

  shardId = result.shardid();
  ProtobufToDSBlock(result.dsblock(), dsBlock);

  for (const auto& proto_vcblock : result.vcblocks()) {
    VCBlock vcblock;
    ProtobufToVCBlock(proto_vcblock, vcblock);
    vcBlocks.emplace_back(move(vcblock));
  }

  ProtobufToShardingStructure(result.sharding(), shards);

  ProtobufToTxSharingAssignments(result.assignments(), dsReceivers,
                                 shardReceivers, shardSenders);

  return true;
}

bool Messenger::SetNodeFinalBlock(vector<unsigned char>& dst,
                                  const unsigned int offset,
                                  const uint32_t shardId,
                                  const uint64_t dsBlockNumber,
                                  const uint32_t consensusID,
                                  const TxBlock& txBlock,
                                  const vector<unsigned char>& stateDelta) {
  LOG_MARKER();

  NodeFinalBlock result;

  result.set_shardid(shardId);
  result.set_dsblocknumber(dsBlockNumber);
  result.set_consensusid(consensusID);
  TxBlockToProtobuf(txBlock, *result.mutable_txblock());
  result.set_statedelta(stateDelta.data(), stateDelta.size());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeFinalBlock initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetNodeFinalBlock(const vector<unsigned char>& src,
                                  const unsigned int offset, uint32_t& shardId,
                                  uint64_t& dsBlockNumber,
                                  uint32_t& consensusID, TxBlock& txBlock,
                                  vector<unsigned char>& stateDelta) {
  LOG_MARKER();

  NodeFinalBlock result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeFinalBlock initialization failed.");
    return false;
  }

  shardId = result.shardid();
  dsBlockNumber = result.dsblocknumber();
  consensusID = result.consensusid();
  ProtobufToTxBlock(result.txblock(), txBlock);
  stateDelta.resize(result.statedelta().size());
  copy(result.statedelta().begin(), result.statedelta().end(),
       stateDelta.begin());

  return true;
}

bool Messenger::SetNodeForwardTransaction(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint64_t blockNum, const MicroBlockHashSet& hashes,
    const uint32_t& shardId, const vector<TransactionWithReceipt>& txns) {
  LOG_MARKER();

  NodeForwardTransaction result;

  result.set_blocknum(blockNum);
  result.set_microblocktxhash(hashes.m_txRootHash.asArray().data(),
                              hashes.m_txRootHash.asArray().size());
  result.set_microblockdeltahash(hashes.m_stateDeltaHash.asArray().data(),
                                 hashes.m_stateDeltaHash.asArray().size());
  result.set_microblockreceipthash(hashes.m_tranReceiptHash.asArray().data(),
                                   hashes.m_tranReceiptHash.asArray().size());
  result.set_shardid(shardId);

  unsigned int txnsCount = 0;

  for (const auto& txn : txns) {
    SerializableToProtobufByteArray(txn, *result.add_txnswithreceipt());
    txnsCount++;
  }

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeForwardTransaction initialization failed.");
    return false;
  }

  LOG_GENERAL(INFO, "BlockNum: " << blockNum << " shardId: " << shardId
                                 << " Hashes: " << hashes
                                 << " Txns: " << txnsCount);

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetNodeForwardTransaction(const vector<unsigned char>& src,
                                          const unsigned int offset,
                                          ForwardedTxnEntry& entry) {
  LOG_MARKER();

  NodeForwardTransaction result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeForwardTransaction initialization failed.");
    return false;
  }

  entry.m_blockNum = result.blocknum();

  TxnHash txRootHash;
  StateHash stateDeltaHash;
  TxnHash tranReceiptHash;

  copy(result.microblocktxhash().begin(), result.microblocktxhash().end(),
       txRootHash.asArray().begin());
  copy(result.microblockdeltahash().begin(), result.microblockdeltahash().end(),
       stateDeltaHash.asArray().begin());
  copy(result.microblockreceipthash().begin(),
       result.microblockreceipthash().end(), tranReceiptHash.asArray().begin());

  entry.m_hash = {txRootHash, stateDeltaHash, tranReceiptHash};

  entry.m_shardId = result.shardid();

  unsigned int txnsCount = 0;

  for (const auto& txn : result.txnswithreceipt()) {
    TransactionWithReceipt txr;
    ProtobufByteArrayToSerializable(txn, txr);
    entry.m_transactions.emplace_back(txr);
    txnsCount++;
  }

  LOG_GENERAL(INFO, entry << endl << " Txns: " << txnsCount);

  return true;
}

bool Messenger::SetNodeVCBlock(vector<unsigned char>& dst,
                               const unsigned int offset,
                               const VCBlock& vcBlock) {
  LOG_MARKER();

  NodeVCBlock result;

  VCBlockToProtobuf(vcBlock, *result.mutable_vcblock());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeVCBlock initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetNodeVCBlock(const vector<unsigned char>& src,
                               const unsigned int offset, VCBlock& vcBlock) {
  LOG_MARKER();

  NodeVCBlock result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeVCBlock initialization failed.");
    return false;
  }

  ProtobufToVCBlock(result.vcblock(), vcBlock);

  return true;
}

bool Messenger::SetNodeForwardTxnBlock(
    std::vector<unsigned char>& dst, const unsigned int offset,
    const uint64_t epochNumber, const uint32_t shardId,
    const std::pair<PrivKey, PubKey>& lookupKey,
    const std::vector<Transaction>& txnsCurrent,
    const std::vector<unsigned char>& txnsGenerated) {
  LOG_MARKER();

  NodeForwardTxnBlock result;

  result.set_epochnumber(epochNumber);
  result.set_shardid(shardId);
  SerializableToProtobufByteArray(lookupKey.second, *result.mutable_pubkey());

  unsigned int txnsCurrentCount = 0;
  unsigned int txnsGeneratedCount = 0;

  for (const auto& txn : txnsCurrent) {
    SerializableToProtobufByteArray(txn, *result.add_transactions());
    txnsCurrentCount++;
  }

  unsigned int txnStreamOffset = 0;
  while (txnStreamOffset < txnsGenerated.size()) {
    Transaction txn;
    if (txn.Deserialize(txnsGenerated, txnStreamOffset) != 0) {
      LOG_GENERAL(WARNING, "Failed to deserialize generated transaction.");
      return false;
    }

    SerializableToProtobufByteArray(txn, *result.add_transactions());

    txnStreamOffset += txn.GetSerializedSize();
    txnsGeneratedCount++;
  }

  Signature signature;
  if (result.transactions().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.transactions(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize transactions.");
      return false;
    }
    if (!Schnorr::GetInstance().Sign(tmp, lookupKey.first, lookupKey.second,
                                     signature)) {
      LOG_GENERAL(WARNING, "Failed to sign transactions.");
      return false;
    }
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeForwardTxnBlock initialization failed.");
    return false;
  }

  LOG_GENERAL(INFO, "Epoch: " << epochNumber << " shardId: " << shardId
                              << " Current txns: " << txnsCurrentCount
                              << " Generated txns: " << txnsGeneratedCount);

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetNodeForwardTxnBlock(const std::vector<unsigned char>& src,
                                       const unsigned int offset,
                                       uint64_t& epochNumber, uint32_t& shardId,
                                       PubKey& lookupPubKey,
                                       std::vector<Transaction>& txns) {
  LOG_MARKER();

  NodeForwardTxnBlock result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeForwardTxnBlock initialization failed.");
    return false;
  }

  epochNumber = result.epochnumber();
  shardId = result.shardid();
  ProtobufByteArrayToSerializable(result.pubkey(), lookupPubKey);

  if (result.transactions().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.transactions(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize transactions.");
      return false;
    }
    Signature signature;
    ProtobufByteArrayToSerializable(result.signature(), signature);

    if (!Schnorr::GetInstance().Verify(tmp, signature, lookupPubKey)) {
      LOG_GENERAL(WARNING, "Invalid signature in transactions.");
      return false;
    }

    for (const auto& txn : result.transactions()) {
      Transaction t;
      ProtobufByteArrayToSerializable(txn, t);
      txns.emplace_back(t);
    }
  }

  LOG_GENERAL(INFO, "Epoch: " << epochNumber << " Shard: " << shardId
                              << " Received txns: " << txns.size());

  return true;
}

bool Messenger::SetNodeMicroBlockAnnouncement(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const pair<PrivKey, PubKey>& leaderKey, const MicroBlock& microBlock,
    vector<unsigned char>& messageToCosign) {
  LOG_MARKER();

  ConsensusAnnouncement announcement;

  // Set the MicroBlock announcement parameters

  NodeMicroBlockAnnouncement* microblock = announcement.mutable_microblock();
  MicroBlockToProtobuf(microBlock, *microblock->mutable_microblock());

  if (!microblock->IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeMicroBlockAnnouncement initialization failed.");
    return false;
  }

  // Set the common consensus announcement parameters

  if (!SetConsensusAnnouncementCore(announcement, consensusID, blockNumber,
                                    blockHash, leaderID, leaderKey)) {
    LOG_GENERAL(WARNING, "SetConsensusAnnouncementCore failed.");
    return false;
  }

  // Serialize the part of the announcement that should be co-signed during the
  // first round of consensus

  messageToCosign.clear();
  if (!microBlock.GetHeader().Serialize(messageToCosign, 0)) {
    LOG_GENERAL(WARNING, "MicroBlockHeader serialization failed.");
    return false;
  }

  // Serialize the announcement

  return SerializeToArray(announcement, dst, offset);
}

bool Messenger::GetNodeMicroBlockAnnouncement(
    const vector<unsigned char>& src, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const PubKey& leaderKey, MicroBlock& microBlock,
    vector<unsigned char>& messageToCosign) {
  LOG_MARKER();

  ConsensusAnnouncement announcement;

  announcement.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!announcement.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusAnnouncement initialization failed.");
    return false;
  }

  if (!announcement.has_microblock()) {
    LOG_GENERAL(WARNING, "NodeMicroBlockAnnouncement initialization failed.");
    return false;
  }

  // Check the common consensus announcement parameters

  if (!GetConsensusAnnouncementCore(announcement, consensusID, blockNumber,
                                    blockHash, leaderID, leaderKey)) {
    LOG_GENERAL(WARNING, "GetConsensusAnnouncementCore failed.");
    return false;
  }

  // Get the MicroBlock announcement parameters

  const NodeMicroBlockAnnouncement& microblock = announcement.microblock();
  ProtobufToMicroBlock(microblock.microblock(), microBlock);

  // Get the part of the announcement that should be co-signed during the first
  // round of consensus

  messageToCosign.clear();
  if (!microBlock.GetHeader().Serialize(messageToCosign, 0)) {
    LOG_GENERAL(WARNING, "MicroBlockHeader serialization failed.");
    return false;
  }

  return true;
}

bool Messenger::SetNodeFallbackBlockAnnouncement(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const pair<PrivKey, PubKey>& leaderKey, const FallbackBlock& fallbackBlock,
    vector<unsigned char>& messageToCosign) {
  LOG_MARKER();

  ConsensusAnnouncement announcement;

  // Set the FallbackBlock announcement parameters

  NodeFallbackBlockAnnouncement* fallbackblock =
      announcement.mutable_fallbackblock();
  SerializableToProtobufByteArray(fallbackBlock,
                                  *fallbackblock->mutable_fallbackblock());

  if (!fallbackblock->IsInitialized()) {
    LOG_GENERAL(WARNING,
                "NodeFallbackBlockAnnouncement initialization failed.");
    return false;
  }

  // Set the common consensus announcement parameters

  if (!SetConsensusAnnouncementCore(announcement, consensusID, blockNumber,
                                    blockHash, leaderID, leaderKey)) {
    LOG_GENERAL(WARNING, "SetConsensusAnnouncementCore failed.");
    return false;
  }

  // Serialize the part of the announcement that should be co-signed during the
  // first round of consensus

  messageToCosign.clear();
  if (!fallbackBlock.GetHeader().Serialize(messageToCosign, 0)) {
    LOG_GENERAL(WARNING, "FallbackBlockHeader serialization failed.");
    return false;
  }

  // Serialize the announcement

  return SerializeToArray(announcement, dst, offset);
}

bool Messenger::GetNodeFallbackBlockAnnouncement(
    const vector<unsigned char>& src, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const PubKey& leaderKey, FallbackBlock& fallbackBlock,
    vector<unsigned char>& messageToCosign) {
  LOG_MARKER();

  ConsensusAnnouncement announcement;

  announcement.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!announcement.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusAnnouncement initialization failed.");
    return false;
  }

  if (!announcement.has_fallbackblock()) {
    LOG_GENERAL(WARNING,
                "NodeFallbackBlockAnnouncement initialization failed.");
    return false;
  }

  // Check the common consensus announcement parameters

  if (!GetConsensusAnnouncementCore(announcement, consensusID, blockNumber,
                                    blockHash, leaderID, leaderKey)) {
    LOG_GENERAL(WARNING, "GetConsensusAnnouncementCore failed.");
    return false;
  }

  // Get the FallbackBlock announcement parameters

  const NodeFallbackBlockAnnouncement& fallbackblock =
      announcement.fallbackblock();
  ProtobufByteArrayToSerializable(fallbackblock.fallbackblock(), fallbackBlock);

  // Get the part of the announcement that should be co-signed during the first
  // round of consensus

  messageToCosign.clear();
  if (!fallbackBlock.GetHeader().Serialize(messageToCosign, 0)) {
    LOG_GENERAL(WARNING, "FallbackBlockHeader serialization failed.");
    return false;
  }

  return true;
}

bool Messenger::SetNodeFallbackBlock(vector<unsigned char>& dst,
                                     const unsigned int offset,
                                     const FallbackBlock& fallbackBlock) {
  LOG_MARKER();

  NodeFallbackBlock result;

  FallbackBlockToProtobuf(fallbackBlock, *result.mutable_fallbackblock());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeFallbackBlock initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetNodeFallbackBlock(const vector<unsigned char>& src,
                                     const unsigned int offset,
                                     FallbackBlock& fallbackBlock) {
  LOG_MARKER();

  NodeFallbackBlock result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "NodeFallbackBlock initialization failed.");
    return false;
  }

  ProtobufToFallbackBlock(result.fallbackblock(), fallbackBlock);

  return true;
}

// ============================================================================
// Lookup messages
// ============================================================================

bool Messenger::SetLookupGetSeedPeers(vector<unsigned char>& dst,
                                      const unsigned int offset,
                                      const uint32_t listenPort) {
  LOG_MARKER();

  LookupGetSeedPeers result;

  result.set_listenport(listenPort);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetSeedPeers initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupGetSeedPeers(const vector<unsigned char>& src,
                                      const unsigned int offset,
                                      uint32_t& listenPort) {
  LOG_MARKER();

  LookupGetSeedPeers result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetSeedPeers initialization failed.");
    return false;
  }

  listenPort = result.listenport();

  return true;
}

bool Messenger::SetLookupSetSeedPeers(
    vector<unsigned char>& dst, const unsigned int offset,
    const std::pair<PrivKey, PubKey>& lookupKey,
    const vector<Peer>& candidateSeeds) {
  LOG_MARKER();

  LookupSetSeedPeers result;

  unordered_set<uint32_t> indicesAlreadyAdded;

  random_device rd;
  mt19937 gen(rd());
  uniform_int_distribution<> dis(0, candidateSeeds.size() - 1);

  for (unsigned int i = 0; i < candidateSeeds.size(); i++) {
    uint32_t index = dis(gen);
    while (indicesAlreadyAdded.find(index) != indicesAlreadyAdded.end()) {
      index = dis(gen);
    }
    indicesAlreadyAdded.insert(index);

    SerializableToProtobufByteArray(candidateSeeds.at(index),
                                    *result.add_candidateseeds());
  }

  SerializableToProtobufByteArray(lookupKey.second, *result.mutable_pubkey());

  Signature signature;
  if (result.candidateseeds().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.candidateseeds(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize candidate seeds.");
      return false;
    }
    if (!Schnorr::GetInstance().Sign(tmp, lookupKey.first, lookupKey.second,
                                     signature)) {
      LOG_GENERAL(WARNING, "Failed to sign candidate seeds.");
      return false;
    }
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetSeedPeers initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetSeedPeers(const vector<unsigned char>& src,
                                      const unsigned int offset,
                                      PubKey& lookupPubKey,
                                      vector<Peer>& candidateSeeds) {
  LOG_MARKER();

  LookupSetSeedPeers result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetSeedPeers initialization failed.");
    return false;
  }

  ProtobufByteArrayToSerializable(result.pubkey(), lookupPubKey);

  for (const auto& peer : result.candidateseeds()) {
    Peer seedPeer;
    ProtobufByteArrayToSerializable(peer, seedPeer);
    candidateSeeds.emplace_back(seedPeer);
  }

  Signature signature;
  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (result.candidateseeds().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.candidateseeds(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize candidate seeds.");
      return false;
    }

    if (!Schnorr::GetInstance().Verify(tmp, signature, lookupPubKey)) {
      LOG_GENERAL(WARNING, "Invalid signature in candidate seeds.");
      return false;
    }
  }

  return true;
}

bool Messenger::SetLookupGetDSInfoFromSeed(vector<unsigned char>& dst,
                                           const unsigned int offset,
                                           const uint32_t listenPort) {
  LOG_MARKER();

  LookupGetDSInfoFromSeed result;

  result.set_listenport(listenPort);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetDSInfoFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupGetDSInfoFromSeed(const vector<unsigned char>& src,
                                           const unsigned int offset,
                                           uint32_t& listenPort) {
  LOG_MARKER();

  LookupGetDSInfoFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetDSInfoFromSeed initialization failed.");
    return false;
  }

  listenPort = result.listenport();

  return true;
}

bool Messenger::SetLookupSetDSInfoFromSeed(
    vector<unsigned char>& dst, const unsigned int offset,
    const std::pair<PrivKey, PubKey>& senderKey,
    const deque<pair<PubKey, Peer>>& dsNodes) {
  LOG_MARKER();

  LookupSetDSInfoFromSeed result;

  DSCommitteeToProtobuf(dsNodes, *result.mutable_dscommittee());

  SerializableToProtobufByteArray(senderKey.second, *result.mutable_pubkey());

  std::vector<unsigned char> tmp;
  if (!SerializeToArray(result.dscommittee(), tmp, 0)) {
    LOG_GENERAL(WARNING, "Failed to serialize DS committee.");
    return false;
  }

  Signature signature;
  if (!Schnorr::GetInstance().Sign(tmp, senderKey.first, senderKey.second,
                                   signature)) {
    LOG_GENERAL(WARNING, "Failed to sign DS committee.");
    return false;
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetDSInfoFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetDSInfoFromSeed(const vector<unsigned char>& src,
                                           const unsigned int offset,
                                           PubKey& senderPubKey,
                                           deque<pair<PubKey, Peer>>& dsNodes) {
  LOG_MARKER();

  LookupSetDSInfoFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);
  ProtobufByteArrayToSerializable(result.pubkey(), senderPubKey);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetDSInfoFromSeed initialization failed.");
    return false;
  }

  ProtobufToDSCommittee(result.dscommittee(), dsNodes);

  Signature signature;
  ProtobufByteArrayToSerializable(result.signature(), signature);

  std::vector<unsigned char> tmp;
  if (!SerializeToArray(result.dscommittee(), tmp, 0)) {
    LOG_GENERAL(WARNING, "Failed to serialize DS committee.");
    return false;
  }

  if (!Schnorr::GetInstance().Verify(tmp, signature, senderPubKey)) {
    LOG_GENERAL(WARNING, "Invalid signature in DS nodes info.");
    return false;
  }

  return true;
}

bool Messenger::SetLookupGetDSBlockFromSeed(vector<unsigned char>& dst,
                                            const unsigned int offset,
                                            const uint64_t lowBlockNum,
                                            const uint64_t highBlockNum,
                                            const uint32_t listenPort) {
  LOG_MARKER();

  LookupGetDSBlockFromSeed result;

  result.set_lowblocknum(lowBlockNum);
  result.set_highblocknum(highBlockNum);
  result.set_listenport(listenPort);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetDSBlockFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupGetDSBlockFromSeed(const vector<unsigned char>& src,
                                            const unsigned int offset,
                                            uint64_t& lowBlockNum,
                                            uint64_t& highBlockNum,
                                            uint32_t& listenPort) {
  LOG_MARKER();

  LookupGetDSBlockFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetDSBlockFromSeed initialization failed.");
    return false;
  }

  lowBlockNum = result.lowblocknum();
  highBlockNum = result.highblocknum();
  listenPort = result.listenport();

  return true;
}

bool Messenger::SetLookupSetDSBlockFromSeed(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint64_t lowBlockNum, const uint64_t highBlockNum,
    const std::pair<PrivKey, PubKey>& lookupKey,
    const vector<DSBlock>& dsBlocks) {
  LOG_MARKER();

  LookupSetDSBlockFromSeed result;

  result.set_lowblocknum(lowBlockNum);
  result.set_highblocknum(highBlockNum);

  SerializableToProtobufByteArray(lookupKey.second, *result.mutable_pubkey());

  for (const auto& dsblock : dsBlocks) {
    DSBlockToProtobuf(dsblock, *result.add_dsblocks());
  }

  Signature signature;
  if (result.dsblocks().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.dsblocks(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize DS blocks.");
      return false;
    }

    if (!Schnorr::GetInstance().Sign(tmp, lookupKey.first, lookupKey.second,
                                     signature)) {
      LOG_GENERAL(WARNING, "Failed to sign DS blocks.");
      return false;
    }
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetDSBlockFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetDSBlockFromSeed(const vector<unsigned char>& src,
                                            const unsigned int offset,
                                            uint64_t& lowBlockNum,
                                            uint64_t& highBlockNum,
                                            PubKey& lookupPubKey,
                                            vector<DSBlock>& dsBlocks) {
  LOG_MARKER();

  LookupSetDSBlockFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetDSBlockFromSeed initialization failed.");
    return false;
  }

  lowBlockNum = result.lowblocknum();
  highBlockNum = result.highblocknum();
  ProtobufByteArrayToSerializable(result.pubkey(), lookupPubKey);

  for (const auto& proto_dsblock : result.dsblocks()) {
    DSBlock dsblock;
    ProtobufToDSBlock(proto_dsblock, dsblock);
    dsBlocks.emplace_back(dsblock);
  }

  Signature signature;
  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (result.dsblocks().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.dsblocks(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize DS blocks.");
      return false;
    }

    if (!Schnorr::GetInstance().Verify(tmp, signature, lookupPubKey)) {
      LOG_GENERAL(WARNING, "Invalid signature in DS blocks.");
      return false;
    }
  }

  return true;
}

bool Messenger::SetLookupGetTxBlockFromSeed(vector<unsigned char>& dst,
                                            const unsigned int offset,
                                            const uint64_t lowBlockNum,
                                            const uint64_t highBlockNum,
                                            const uint32_t listenPort) {
  LOG_MARKER();

  LookupGetTxBlockFromSeed result;

  result.set_lowblocknum(lowBlockNum);
  result.set_highblocknum(highBlockNum);
  result.set_listenport(listenPort);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetTxBlockFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupGetTxBlockFromSeed(const vector<unsigned char>& src,
                                            const unsigned int offset,
                                            uint64_t& lowBlockNum,
                                            uint64_t& highBlockNum,
                                            uint32_t& listenPort) {
  LOG_MARKER();

  LookupGetTxBlockFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetTxBlockFromSeed initialization failed.");
    return false;
  }

  lowBlockNum = result.lowblocknum();
  highBlockNum = result.highblocknum();
  listenPort = result.listenport();

  return true;
}

bool Messenger::SetLookupSetTxBlockFromSeed(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint64_t lowBlockNum, const uint64_t highBlockNum,
    const std::pair<PrivKey, PubKey>& lookupKey,
    const vector<TxBlock>& txBlocks) {
  LOG_MARKER();

  LookupSetTxBlockFromSeed result;

  result.set_lowblocknum(lowBlockNum);
  result.set_highblocknum(highBlockNum);

  for (const auto& txblock : txBlocks) {
    TxBlockToProtobuf(txblock, *result.add_txblocks());
  }

  SerializableToProtobufByteArray(lookupKey.second, *result.mutable_pubkey());

  Signature signature;
  if (result.txblocks().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.txblocks(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize tx blocks.");
      return false;
    }

    if (!Schnorr::GetInstance().Sign(tmp, lookupKey.first, lookupKey.second,
                                     signature)) {
      LOG_GENERAL(WARNING, "Failed to sign tx blocks.");
      return false;
    }
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetTxBlockFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetTxBlockFromSeed(const vector<unsigned char>& src,
                                            const unsigned int offset,
                                            uint64_t& lowBlockNum,
                                            uint64_t& highBlockNum,
                                            PubKey& lookupPubKey,
                                            vector<TxBlock>& txBlocks) {
  LOG_MARKER();

  LookupSetTxBlockFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetTxBlockFromSeed initialization failed.");
    return false;
  }

  lowBlockNum = result.lowblocknum();
  highBlockNum = result.highblocknum();

  for (const auto& txblock : result.txblocks()) {
    TxBlock block;
    ProtobufToTxBlock(txblock, block);
    txBlocks.emplace_back(block);
  }

  ProtobufByteArrayToSerializable(result.pubkey(), lookupPubKey);
  Signature signature;
  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (result.txblocks().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.txblocks(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize tx blocks.");
      return false;
    }

    if (!Schnorr::GetInstance().Verify(tmp, signature, lookupPubKey)) {
      LOG_GENERAL(WARNING, "Invalid signature in tx blocks.");
      return false;
    }
  }

  return true;
}

bool Messenger::SetLookupGetTxBodyFromSeed(vector<unsigned char>& dst,
                                           const unsigned int offset,
                                           const vector<unsigned char>& txHash,
                                           const uint32_t listenPort) {
  LOG_MARKER();

  LookupGetTxBodyFromSeed result;

  result.set_txhash(txHash.data(), txHash.size());
  result.set_listenport(listenPort);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetTxBodyFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupGetTxBodyFromSeed(const vector<unsigned char>& src,
                                           const unsigned int offset,
                                           TxnHash& txHash,
                                           uint32_t& listenPort) {
  LOG_MARKER();

  LookupGetTxBodyFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetTxBodyFromSeed initialization failed.");
    return false;
  }

  copy(result.txhash().begin(), result.txhash().end(),
       txHash.asArray().begin());
  listenPort = result.listenport();

  return true;
}

bool Messenger::SetLookupSetTxBodyFromSeed(
    vector<unsigned char>& dst, const unsigned int offset,
    const TxnHash& txHash, const TransactionWithReceipt& txBody) {
  LOG_MARKER();

  LookupSetTxBodyFromSeed result;

  result.set_txhash(txHash.data(), txHash.size);
  SerializableToProtobufByteArray(txBody, *result.mutable_txbody());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetTxBodyFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetTxBodyFromSeed(const vector<unsigned char>& src,
                                           const unsigned int offset,
                                           TxnHash& txHash,
                                           TransactionWithReceipt& txBody) {
  LOG_MARKER();

  LookupSetTxBodyFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetTxBodyFromSeed initialization failed.");
    return false;
  }

  copy(result.txhash().begin(), result.txhash().end(),
       txHash.asArray().begin());
  ProtobufByteArrayToSerializable(result.txbody(), txBody);

  return true;
}

bool Messenger::SetLookupSetNetworkIDFromSeed(vector<unsigned char>& dst,
                                              const unsigned int offset,
                                              const string& networkID) {
  LOG_MARKER();

  LookupSetNetworkIDFromSeed result;

  result.set_networkid(networkID);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetNetworkIDFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetNetworkIDFromSeed(const vector<unsigned char>& src,
                                              const unsigned int offset,
                                              string& networkID) {
  LOG_MARKER();

  LookupSetNetworkIDFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetNetworkIDFromSeed initialization failed.");
    return false;
  }

  networkID = result.networkid();

  return true;
}

bool Messenger::SetLookupGetStateFromSeed(vector<unsigned char>& dst,
                                          const unsigned int offset,
                                          const uint32_t listenPort) {
  LOG_MARKER();

  LookupGetStateFromSeed result;

  result.set_listenport(listenPort);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetStateFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupGetStateFromSeed(const vector<unsigned char>& src,
                                          const unsigned int offset,
                                          uint32_t& listenPort) {
  LOG_MARKER();

  LookupGetStateFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetStateFromSeed initialization failed.");
    return false;
  }

  listenPort = result.listenport();

  return true;
}

bool Messenger::SetLookupSetStateFromSeed(
    vector<unsigned char>& dst, const unsigned int offset,
    const std::pair<PrivKey, PubKey>& lookupKey,
    const AccountStore& accountStore) {
  LOG_MARKER();

  LookupSetStateFromSeed result;

  SerializableToProtobufByteArray(accountStore, *result.mutable_accounts());

  SerializableToProtobufByteArray(lookupKey.second, *result.mutable_pubkey());
  Signature signature;

  std::vector<unsigned char> tmp;
  if (!SerializeToArray(result.accounts(), tmp, 0)) {
    LOG_GENERAL(WARNING, "Failed to serialize accounts.");
    return false;
  }

  if (!Schnorr::GetInstance().Sign(tmp, lookupKey.first, lookupKey.second,
                                   signature)) {
    LOG_GENERAL(WARNING, "Failed to sign accounts.");
    return false;
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetStateFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetStateFromSeed(const vector<unsigned char>& src,
                                          const unsigned int offset,
                                          PubKey& lookupPubKey,
                                          AccountStore& accountStore) {
  LOG_MARKER();

  LookupSetStateFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetStateFromSeed initialization failed.");
    return false;
  }

  ProtobufByteArrayToSerializable(result.accounts(), accountStore);

  ProtobufByteArrayToSerializable(result.pubkey(), lookupPubKey);
  Signature signature;
  ProtobufByteArrayToSerializable(result.signature(), signature);

  std::vector<unsigned char> tmp;
  if (!SerializeToArray(result.accounts(), tmp, 0)) {
    LOG_GENERAL(WARNING, "Failed to serialize accounts.");
    return false;
  }

  if (!Schnorr::GetInstance().Verify(tmp, signature, lookupPubKey)) {
    LOG_GENERAL(WARNING, "Invalid signature in accounts.");
    return false;
  }

  return true;
}

bool Messenger::SetLookupSetLookupOffline(vector<unsigned char>& dst,
                                          const unsigned int offset,
                                          const uint32_t listenPort) {
  LOG_MARKER();

  LookupSetLookupOffline result;

  result.set_listenport(listenPort);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetLookupOffline initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetLookupOffline(const vector<unsigned char>& src,
                                          const unsigned int offset,
                                          uint32_t& listenPort) {
  LOG_MARKER();

  LookupSetLookupOffline result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetLookupOffline initialization failed.");
    return false;
  }

  listenPort = result.listenport();

  return true;
}

bool Messenger::SetLookupSetLookupOnline(vector<unsigned char>& dst,
                                         const unsigned int offset,
                                         const uint32_t listenPort,
                                         const PubKey& pubKey) {
  LOG_MARKER();

  LookupSetLookupOnline result;

  result.set_listenport(listenPort);
  SerializableToProtobufByteArray(pubKey, *result.mutable_pubkey());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetLookupOnline initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetLookupOnline(const vector<unsigned char>& src,
                                         const unsigned int offset,
                                         uint32_t& listenPort, PubKey& pubKey) {
  LOG_MARKER();

  LookupSetLookupOnline result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetLookupOnline initialization failed.");
    return false;
  }

  listenPort = result.listenport();

  ProtobufByteArrayToSerializable(result.pubkey(), pubKey);

  return true;
}

bool Messenger::SetLookupGetOfflineLookups(vector<unsigned char>& dst,
                                           const unsigned int offset,
                                           const uint32_t listenPort) {
  LOG_MARKER();

  LookupGetOfflineLookups result;

  result.set_listenport(listenPort);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetOfflineLookups initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupGetOfflineLookups(const vector<unsigned char>& src,
                                           const unsigned int offset,
                                           uint32_t& listenPort) {
  LOG_MARKER();

  LookupGetOfflineLookups result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetOfflineLookups initialization failed.");
    return false;
  }

  listenPort = result.listenport();

  return true;
}

bool Messenger::SetLookupSetOfflineLookups(
    vector<unsigned char>& dst, const unsigned int offset,
    const std::pair<PrivKey, PubKey>& lookupKey, const vector<Peer>& nodes) {
  LOG_MARKER();

  LookupSetOfflineLookups result;

  for (const auto& node : nodes) {
    SerializableToProtobufByteArray(node, *result.add_nodes());
  }

  SerializableToProtobufByteArray(lookupKey.second, *result.mutable_pubkey());
  Signature signature;
  if (result.nodes().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.nodes(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize offline lookup nodes.");
      return false;
    }

    if (!Schnorr::GetInstance().Sign(tmp, lookupKey.first, lookupKey.second,
                                     signature)) {
      LOG_GENERAL(WARNING, "Failed to sign offline lookup nodes.");
      return false;
    }
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetOfflineLookups initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetOfflineLookups(const vector<unsigned char>& src,
                                           const unsigned int offset,
                                           PubKey& lookupPubKey,
                                           vector<Peer>& nodes) {
  LOG_MARKER();

  LookupSetOfflineLookups result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetOfflineLookups initialization failed.");
    return false;
  }

  for (const auto& lookup : result.nodes()) {
    Peer node;
    ProtobufByteArrayToSerializable(lookup, node);
    nodes.emplace_back(node);
  }

  ProtobufByteArrayToSerializable(result.pubkey(), lookupPubKey);
  Signature signature;
  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (result.nodes().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.nodes(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize offline lookup nodes.");
      return false;
    }

    if (!Schnorr::GetInstance().Verify(tmp, signature, lookupPubKey)) {
      LOG_GENERAL(WARNING, "Invalid signature in offline lookup nodes.");
      return false;
    }
  }

  return true;
}

bool Messenger::SetLookupGetStartPoWFromSeed(vector<unsigned char>& dst,
                                             const unsigned int offset,
                                             const uint32_t listenPort) {
  LOG_MARKER();

  LookupGetStartPoWFromSeed result;

  result.set_listenport(listenPort);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetStartPoWFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupGetStartPoWFromSeed(const vector<unsigned char>& src,
                                             const unsigned int offset,
                                             uint32_t& listenPort) {
  LOG_MARKER();

  LookupGetStartPoWFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetStartPoWFromSeed initialization failed.");
    return false;
  }

  listenPort = result.listenport();

  return true;
}

bool Messenger::SetLookupSetStartPoWFromSeed(
    std::vector<unsigned char>& dst, const unsigned int offset,
    const uint64_t blockNumber, const std::pair<PrivKey, PubKey>& lookupKey) {
  LOG_MARKER();

  LookupSetStartPoWFromSeed result;

  result.set_blocknumber(blockNumber);
  SerializableToProtobufByteArray(lookupKey.second, *result.mutable_pubkey());

  std::vector<unsigned char> tmp;
  NumberToArray<uint64_t, sizeof(uint64_t)>(blockNumber, tmp, 0);

  Signature signature;
  if (!Schnorr::GetInstance().Sign(tmp, lookupKey.first, lookupKey.second,
                                   signature)) {
    LOG_GENERAL(WARNING, "Failed to sign start PoW message.");
    return false;
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetStartPoWFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetStartPoWFromSeed(
    const std::vector<unsigned char>& src, const unsigned int offset,
    PubKey& lookupPubKey) {
  LOG_MARKER();

  LookupSetStartPoWFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetStartPoWFromSeed initialization failed.");
    return false;
  }

  std::vector<unsigned char> tmp;
  NumberToArray<uint64_t, sizeof(uint64_t)>(result.blocknumber(), tmp, 0);

  ProtobufByteArrayToSerializable(result.pubkey(), lookupPubKey);
  Signature signature;
  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (!Schnorr::GetInstance().Verify(tmp, signature, lookupPubKey)) {
    LOG_GENERAL(WARNING, "Invalid signature in start PoW message.");
    return false;
  }

  return true;
}

bool Messenger::SetLookupGetShardsFromSeed(vector<unsigned char>& dst,
                                           const unsigned int offset,
                                           const uint32_t listenPort) {
  LOG_MARKER();

  LookupGetShardsFromSeed result;

  result.set_listenport(listenPort);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetShardsFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupGetShardsFromSeed(const vector<unsigned char>& src,
                                           const unsigned int offset,
                                           uint32_t& listenPort) {
  LOG_MARKER();

  LookupGetShardsFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetShardsFromSeed initialization failed.");
    return false;
  }

  listenPort = result.listenport();

  return true;
}

bool Messenger::SetLookupSetShardsFromSeed(
    vector<unsigned char>& dst, const unsigned int offset,
    const std::pair<PrivKey, PubKey>& lookupKey, const DequeOfShard& shards) {
  LOG_MARKER();

  LookupSetShardsFromSeed result;

  ShardingStructureToProtobuf(shards, *result.mutable_sharding());

  SerializableToProtobufByteArray(lookupKey.second, *result.mutable_pubkey());
  Signature signature;
  std::vector<unsigned char> tmp;
  if (!SerializeToArray(result.sharding(), tmp, 0)) {
    LOG_GENERAL(WARNING, "Failed to serialize sharding structure.");
    return false;
  }

  if (!Schnorr::GetInstance().Sign(tmp, lookupKey.first, lookupKey.second,
                                   signature)) {
    LOG_GENERAL(WARNING, "Failed to sign sharding structure.");
    return false;
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetShardsFromSeed initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetShardsFromSeed(const vector<unsigned char>& src,
                                           const unsigned int offset,
                                           PubKey& lookupPubKey,
                                           DequeOfShard& shards) {
  LOG_MARKER();

  LookupSetShardsFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetShardsFromSeed initialization failed.");
    return false;
  }

  ProtobufToShardingStructure(result.sharding(), shards);

  ProtobufByteArrayToSerializable(result.pubkey(), lookupPubKey);
  Signature signature;
  ProtobufByteArrayToSerializable(result.signature(), signature);

  std::vector<unsigned char> tmp;
  if (!SerializeToArray(result.sharding(), tmp, 0)) {
    LOG_GENERAL(WARNING, "Failed to serialize sharding structure.");
    return false;
  }

  if (!Schnorr::GetInstance().Verify(tmp, signature, lookupPubKey)) {
    LOG_GENERAL(WARNING, "Invalid signature in sharding structure.");
    return false;
  }

  return true;
}

bool Messenger::SetLookupGetMicroBlockFromLookup(
    vector<unsigned char>& dest, const unsigned int offset,
    const map<uint64_t, vector<uint32_t>>& microBlockInfo, uint32_t portNo) {
  LOG_MARKER();

  LookupGetMicroBlockFromLookup result;

  result.set_portno(portNo);

  for (const auto& mb : microBlockInfo) {
    MicroBlockInfo& res_mb = *result.add_blocknums();
    res_mb.set_blocknum(mb.first);

    for (uint32_t shard_id : mb.second) {
      res_mb.add_shards(shard_id);
    }
  }

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING,
                "LookupGetMicroBlockFromLookup initialization failed.");
    return false;
  }
  return SerializeToArray(result, dest, offset);
}

bool Messenger::GetLookupGetMicroBlockFromLookup(
    const vector<unsigned char>& src, const unsigned int offset,
    map<uint64_t, vector<uint32_t>>& microBlockInfo, uint32_t& portNo) {
  LOG_MARKER();

  LookupGetMicroBlockFromLookup result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING,
                "LookupGetMicroBlockFromLookup initialization failed.");
    return false;
  }

  portNo = result.portno();

  for (const auto& blocknum : result.blocknums()) {
    vector<uint32_t> tempVec;
    for (const auto& id : blocknum.shards()) {
      tempVec.emplace_back(id);
    }
    microBlockInfo.insert(make_pair(blocknum.blocknum(), tempVec));
  }
  return true;
}

bool Messenger::SetLookupSetMicroBlockFromLookup(
    vector<unsigned char>& dst, const unsigned int offset,
    const std::pair<PrivKey, PubKey>& lookupKey,
    const vector<MicroBlock>& mbs) {
  LOG_MARKER();
  LookupSetMicroBlockFromLookup result;

  for (const auto& mb : mbs) {
    MicroBlockToProtobuf(mb, *result.add_microblocks());
  }

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetMicroBlockFromLookup initialization failed");
    return false;
  }

  SerializableToProtobufByteArray(lookupKey.second, *result.mutable_pubkey());
  Signature signature;
  if (result.microblocks().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.microblocks(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize micro blocks.");
      return false;
    }

    if (!Schnorr::GetInstance().Sign(tmp, lookupKey.first, lookupKey.second,
                                     signature)) {
      LOG_GENERAL(WARNING, "Failed to sign micro blocks.");
      return false;
    }
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetMicroBlockFromLookup(
    const vector<unsigned char>& src, const unsigned int offset,
    PubKey& lookupPubKey, vector<MicroBlock>& mbs) {
  LOG_MARKER();
  LookupSetMicroBlockFromLookup result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetMicroBlockFromLookup initialization failed");
    return false;
  }

  ProtobufByteArrayToSerializable(result.pubkey(), lookupPubKey);
  Signature signature;
  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (result.microblocks().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.microblocks(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize micro blocks.");
      return false;
    }

    if (!Schnorr::GetInstance().Verify(tmp, signature, lookupPubKey)) {
      LOG_GENERAL(WARNING, "Invalid signature in micro blocks.");
      return false;
    }
  }

  for (const auto& res_mb : result.microblocks()) {
    MicroBlock mb;

    ProtobufToMicroBlock(res_mb, mb);

    mbs.emplace_back(mb);
  }

  return true;
}

bool Messenger::SetLookupGetTxnsFromLookup(vector<unsigned char>& dst,
                                           const unsigned int offset,
                                           const vector<TxnHash>& txnhashes,
                                           uint32_t portNo) {
  LOG_MARKER();

  LookupGetTxnsFromLookup result;

  result.set_portno(portNo);

  for (const auto& txhash : txnhashes) {
    result.add_txnhashes(txhash.data(), txhash.size);
  }

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetTxnsFromLookup initialization failure");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupGetTxnsFromLookup(const vector<unsigned char>& src,
                                           const unsigned int offset,
                                           vector<TxnHash>& txnhashes,
                                           uint32_t& portNo) {
  LOG_MARKER();

  LookupGetTxnsFromLookup result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  portNo = result.portno();

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupGetTxnsFromLookup initialization failure");
    return false;
  }

  for (const auto& hash : result.txnhashes()) {
    txnhashes.emplace_back();
    unsigned int size =
        min((unsigned int)hash.size(), (unsigned int)txnhashes.back().size);
    copy(hash.begin(), hash.begin() + size, txnhashes.back().asArray().begin());
  }
  return true;
}

bool Messenger::SetLookupSetTxnsFromLookup(
    vector<unsigned char>& dst, const unsigned int offset,
    const std::pair<PrivKey, PubKey>& lookupKey,
    const vector<TransactionWithReceipt>& txns) {
  LOG_MARKER();

  LookupSetTxnsFromLookup result;

  for (auto const& txn : txns) {
    SerializableToProtobufByteArray(txn, *result.add_transactions());
  }

  SerializableToProtobufByteArray(lookupKey.second, *result.mutable_pubkey());
  Signature signature;
  if (result.transactions().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.transactions(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize transactions.");
      return false;
    }

    if (!Schnorr::GetInstance().Sign(tmp, lookupKey.first, lookupKey.second,
                                     signature)) {
      LOG_GENERAL(WARNING, "Failed to sign transactions.");
      return false;
    }
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetTxnsFromLookup initialization failure");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetTxnsFromLookup(
    const vector<unsigned char>& src, const unsigned int offset,
    PubKey& lookupPubKey, vector<TransactionWithReceipt>& txns) {
  LOG_MARKER();

  LookupSetTxnsFromLookup result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "LookupSetTxnsFromLookup initialization failed");
    return false;
  }

  ProtobufByteArrayToSerializable(result.pubkey(), lookupPubKey);
  Signature signature;
  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (result.transactions().size() > 0) {
    std::vector<unsigned char> tmp;
    if (!RepeatableToArray(result.transactions(), tmp, 0)) {
      LOG_GENERAL(WARNING, "Failed to serialize transactions.");
      return false;
    }

    if (!Schnorr::GetInstance().Verify(tmp, signature, lookupPubKey)) {
      LOG_GENERAL(WARNING, "Invalid signature in transactions.");
      return false;
    }
  }

  for (auto const& protoTxn : result.transactions()) {
    TransactionWithReceipt txn;
    ProtobufByteArrayToSerializable(protoTxn, txn);
    txns.emplace_back(txn);
  }

  return true;
}

// ============================================================================
// Consensus messages
// ============================================================================

bool Messenger::SetConsensusCommit(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t backupID,
    const CommitPoint& commit, const pair<PrivKey, PubKey>& backupKey) {
  LOG_MARKER();

  ConsensusCommit result;

  result.mutable_consensusinfo()->set_consensusid(consensusID);
  result.mutable_consensusinfo()->set_blocknumber(blockNumber);
  result.mutable_consensusinfo()->set_blockhash(blockHash.data(),
                                                blockHash.size());
  result.mutable_consensusinfo()->set_backupid(backupID);

  SerializableToProtobufByteArray(
      commit, *result.mutable_consensusinfo()->mutable_commit());

  if (!result.consensusinfo().IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusCommit.Data initialization failed.");
    return false;
  }

  vector<unsigned char> tmp(result.consensusinfo().ByteSize());
  result.consensusinfo().SerializeToArray(tmp.data(), tmp.size());

  Signature signature;

  if (!Schnorr::GetInstance().Sign(tmp, backupKey.first, backupKey.second,
                                   signature)) {
    LOG_GENERAL(WARNING, "Failed to sign commit.");
    return false;
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusCommit initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetConsensusCommit(
    const vector<unsigned char>& src, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, uint16_t& backupID,
    CommitPoint& commit, const deque<pair<PubKey, Peer>>& committeeKeys) {
  LOG_MARKER();

  ConsensusCommit result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusCommit initialization failed.");
    return false;
  }

  if (result.consensusinfo().consensusid() != consensusID) {
    LOG_GENERAL(WARNING, "Consensus ID mismatch. Expected: "
                             << consensusID << " Actual: "
                             << result.consensusinfo().consensusid());
    return false;
  }

  if (result.consensusinfo().blocknumber() != blockNumber) {
    LOG_GENERAL(WARNING, "Block number mismatch. Expected: "
                             << blockNumber << " Actual: "
                             << result.consensusinfo().blocknumber());
    return false;
  }

  if ((result.consensusinfo().blockhash().size() != blockHash.size()) ||
      !equal(blockHash.begin(), blockHash.end(),
             result.consensusinfo().blockhash().begin(),
             [](const unsigned char left, const char right) -> bool {
               return left == (unsigned char)right;
             })) {
    std::vector<unsigned char> remoteBlockHash;
    remoteBlockHash.resize(result.consensusinfo().blockhash().size());
    std::copy(result.consensusinfo().blockhash().begin(),
              result.consensusinfo().blockhash().end(),
              remoteBlockHash.begin());
    LOG_GENERAL(WARNING,
                "Block hash mismatch. Expected: "
                    << DataConversion::Uint8VecToHexStr(blockHash)
                    << " Actual: "
                    << DataConversion::Uint8VecToHexStr(remoteBlockHash));
    return false;
  }

  backupID = result.consensusinfo().backupid();

  if (backupID >= committeeKeys.size()) {
    LOG_GENERAL(WARNING, "Backup ID beyond shard size. Backup ID: "
                             << backupID
                             << " Shard size: " << committeeKeys.size());
    return false;
  }

  ProtobufByteArrayToSerializable(result.consensusinfo().commit(), commit);

  vector<unsigned char> tmp(result.consensusinfo().ByteSize());
  result.consensusinfo().SerializeToArray(tmp.data(), tmp.size());

  Signature signature;

  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (!Schnorr::GetInstance().Verify(tmp, signature,
                                     committeeKeys.at(backupID).first)) {
    LOG_GENERAL(WARNING, "Invalid signature in commit.");
    return false;
  }

  return true;
}

bool Messenger::SetConsensusChallenge(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const CommitPoint& aggregatedCommit, const PubKey& aggregatedKey,
    const Challenge& challenge, const pair<PrivKey, PubKey>& leaderKey) {
  LOG_MARKER();

  ConsensusChallenge result;

  result.mutable_consensusinfo()->set_consensusid(consensusID);
  result.mutable_consensusinfo()->set_blocknumber(blockNumber);
  result.mutable_consensusinfo()->set_blockhash(blockHash.data(),
                                                blockHash.size());
  result.mutable_consensusinfo()->set_leaderid(leaderID);
  SerializableToProtobufByteArray(
      aggregatedCommit,
      *result.mutable_consensusinfo()->mutable_aggregatedcommit());
  SerializableToProtobufByteArray(
      aggregatedKey, *result.mutable_consensusinfo()->mutable_aggregatedkey());
  SerializableToProtobufByteArray(
      challenge, *result.mutable_consensusinfo()->mutable_challenge());

  if (!result.consensusinfo().IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusChallenge.Data initialization failed.");
    return false;
  }

  vector<unsigned char> tmp(result.consensusinfo().ByteSize());
  result.consensusinfo().SerializeToArray(tmp.data(), tmp.size());

  Signature signature;

  if (!Schnorr::GetInstance().Sign(tmp, leaderKey.first, leaderKey.second,
                                   signature)) {
    LOG_GENERAL(WARNING, "Failed to sign challenge.");
    return false;
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusChallenge initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetConsensusChallenge(
    const vector<unsigned char>& src, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    CommitPoint& aggregatedCommit, PubKey& aggregatedKey, Challenge& challenge,
    const PubKey& leaderKey) {
  LOG_MARKER();

  ConsensusChallenge result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusChallenge initialization failed.");
    return false;
  }

  if (result.consensusinfo().consensusid() != consensusID) {
    LOG_GENERAL(WARNING, "Consensus ID mismatch. Expected: "
                             << consensusID << " Actual: "
                             << result.consensusinfo().consensusid());
    return false;
  }

  if (result.consensusinfo().blocknumber() != blockNumber) {
    LOG_GENERAL(WARNING, "Block number mismatch. Expected: "
                             << blockNumber << " Actual: "
                             << result.consensusinfo().blocknumber());
    return false;
  }

  if ((result.consensusinfo().blockhash().size() != blockHash.size()) ||
      !equal(blockHash.begin(), blockHash.end(),
             result.consensusinfo().blockhash().begin(),
             [](const unsigned char left, const char right) -> bool {
               return left == (unsigned char)right;
             })) {
    std::vector<unsigned char> remoteBlockHash;
    remoteBlockHash.resize(result.consensusinfo().blockhash().size());
    std::copy(result.consensusinfo().blockhash().begin(),
              result.consensusinfo().blockhash().end(),
              remoteBlockHash.begin());
    LOG_GENERAL(WARNING,
                "Block hash mismatch. Expected: "
                    << DataConversion::Uint8VecToHexStr(blockHash)
                    << " Actual: "
                    << DataConversion::Uint8VecToHexStr(remoteBlockHash));
    return false;
  }

  if (result.consensusinfo().leaderid() != leaderID) {
    LOG_GENERAL(WARNING, "Leader ID mismatch. Expected: "
                             << leaderID << " Actual: "
                             << result.consensusinfo().leaderid());
    return false;
  }

  ProtobufByteArrayToSerializable(result.consensusinfo().aggregatedcommit(),
                                  aggregatedCommit);
  ProtobufByteArrayToSerializable(result.consensusinfo().aggregatedkey(),
                                  aggregatedKey);
  ProtobufByteArrayToSerializable(result.consensusinfo().challenge(),
                                  challenge);

  vector<unsigned char> tmp(result.consensusinfo().ByteSize());
  result.consensusinfo().SerializeToArray(tmp.data(), tmp.size());

  Signature signature;

  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (!Schnorr::GetInstance().Verify(tmp, signature, leaderKey)) {
    LOG_GENERAL(WARNING, "Invalid signature in challenge.");
    return false;
  }

  return true;
}

bool Messenger::SetConsensusResponse(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t backupID,
    const Response& response, const pair<PrivKey, PubKey>& backupKey) {
  LOG_MARKER();

  ConsensusResponse result;

  result.mutable_consensusinfo()->set_consensusid(consensusID);
  result.mutable_consensusinfo()->set_blocknumber(blockNumber);
  result.mutable_consensusinfo()->set_blockhash(blockHash.data(),
                                                blockHash.size());
  result.mutable_consensusinfo()->set_backupid(backupID);
  SerializableToProtobufByteArray(
      response, *result.mutable_consensusinfo()->mutable_response());

  if (!result.consensusinfo().IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusResponse.Data initialization failed.");
    return false;
  }

  vector<unsigned char> tmp(result.consensusinfo().ByteSize());
  result.consensusinfo().SerializeToArray(tmp.data(), tmp.size());

  Signature signature;

  if (!Schnorr::GetInstance().Sign(tmp, backupKey.first, backupKey.second,
                                   signature)) {
    LOG_GENERAL(WARNING, "Failed to sign response.");
    return false;
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusResponse initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetConsensusResponse(
    const vector<unsigned char>& src, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, uint16_t& backupID,
    Response& response, const deque<pair<PubKey, Peer>>& committeeKeys) {
  LOG_MARKER();

  ConsensusResponse result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusResponse initialization failed.");
    return false;
  }

  if (result.consensusinfo().consensusid() != consensusID) {
    LOG_GENERAL(WARNING, "Consensus ID mismatch. Expected: "
                             << consensusID << " Actual: "
                             << result.consensusinfo().consensusid());
    return false;
  }

  if (result.consensusinfo().blocknumber() != blockNumber) {
    LOG_GENERAL(WARNING, "Block number mismatch. Expected: "
                             << blockNumber << " Actual: "
                             << result.consensusinfo().blocknumber());
    return false;
  }

  if ((result.consensusinfo().blockhash().size() != blockHash.size()) ||
      !equal(blockHash.begin(), blockHash.end(),
             result.consensusinfo().blockhash().begin(),
             [](const unsigned char left, const char right) -> bool {
               return left == (unsigned char)right;
             })) {
    std::vector<unsigned char> remoteBlockHash;
    remoteBlockHash.resize(result.consensusinfo().blockhash().size());
    std::copy(result.consensusinfo().blockhash().begin(),
              result.consensusinfo().blockhash().end(),
              remoteBlockHash.begin());
    LOG_GENERAL(WARNING,
                "Block hash mismatch. Expected: "
                    << DataConversion::Uint8VecToHexStr(blockHash)
                    << " Actual: "
                    << DataConversion::Uint8VecToHexStr(remoteBlockHash));
    return false;
  }

  backupID = result.consensusinfo().backupid();

  if (backupID >= committeeKeys.size()) {
    LOG_GENERAL(WARNING, "Backup ID beyond shard size. Backup ID: "
                             << backupID
                             << " Shard size: " << committeeKeys.size());
    return false;
  }

  ProtobufByteArrayToSerializable(result.consensusinfo().response(), response);

  vector<unsigned char> tmp(result.consensusinfo().ByteSize());
  result.consensusinfo().SerializeToArray(tmp.data(), tmp.size());

  Signature signature;

  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (!Schnorr::GetInstance().Verify(tmp, signature,
                                     committeeKeys.at(backupID).first)) {
    LOG_GENERAL(WARNING, "Invalid signature in response.");
    return false;
  }

  return true;
}

bool Messenger::SetConsensusCollectiveSig(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    const Signature& collectiveSig, const vector<bool>& bitmap,
    const pair<PrivKey, PubKey>& leaderKey) {
  LOG_MARKER();

  ConsensusCollectiveSig result;

  result.mutable_consensusinfo()->set_consensusid(consensusID);
  result.mutable_consensusinfo()->set_blocknumber(blockNumber);
  result.mutable_consensusinfo()->set_blockhash(blockHash.data(),
                                                blockHash.size());
  result.mutable_consensusinfo()->set_leaderid(leaderID);
  SerializableToProtobufByteArray(
      collectiveSig, *result.mutable_consensusinfo()->mutable_collectivesig());
  for (const auto& i : bitmap) {
    result.mutable_consensusinfo()->add_bitmap(i);
  }

  if (!result.consensusinfo().IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusCollectiveSig.Data initialization failed.");
    return false;
  }

  vector<unsigned char> tmp(result.consensusinfo().ByteSize());
  result.consensusinfo().SerializeToArray(tmp.data(), tmp.size());

  Signature signature;

  if (!Schnorr::GetInstance().Sign(tmp, leaderKey.first, leaderKey.second,
                                   signature)) {
    LOG_GENERAL(WARNING, "Failed to sign collectivesig.");
    return false;
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusCollectiveSig initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetConsensusCollectiveSig(
    const vector<unsigned char>& src, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t leaderID,
    vector<bool>& bitmap, Signature& collectiveSig, const PubKey& leaderKey) {
  LOG_MARKER();

  ConsensusCollectiveSig result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusCollectiveSig initialization failed.");
    return false;
  }

  if (result.consensusinfo().consensusid() != consensusID) {
    LOG_GENERAL(WARNING, "Consensus ID mismatch. Expected: "
                             << consensusID << " Actual: "
                             << result.consensusinfo().consensusid());
    return false;
  }

  if (result.consensusinfo().blocknumber() != blockNumber) {
    LOG_GENERAL(WARNING, "Block number mismatch. Expected: "
                             << blockNumber << " Actual: "
                             << result.consensusinfo().blocknumber());
    return false;
  }

  if ((result.consensusinfo().blockhash().size() != blockHash.size()) ||
      !equal(blockHash.begin(), blockHash.end(),
             result.consensusinfo().blockhash().begin(),
             [](const unsigned char left, const char right) -> bool {
               return left == (unsigned char)right;
             })) {
    std::vector<unsigned char> remoteBlockHash;
    remoteBlockHash.resize(result.consensusinfo().blockhash().size());
    std::copy(result.consensusinfo().blockhash().begin(),
              result.consensusinfo().blockhash().end(),
              remoteBlockHash.begin());
    LOG_GENERAL(WARNING,
                "Block hash mismatch. Expected: "
                    << DataConversion::Uint8VecToHexStr(blockHash)
                    << " Actual: "
                    << DataConversion::Uint8VecToHexStr(remoteBlockHash));
    return false;
  }

  if (result.consensusinfo().blocknumber() != blockNumber) {
    LOG_GENERAL(WARNING, "Block number mismatch. Expected: "
                             << blockNumber << " Actual: "
                             << result.consensusinfo().blocknumber());
    return false;
  }

  if (result.consensusinfo().leaderid() != leaderID) {
    LOG_GENERAL(WARNING, "Leader ID mismatch. Expected: "
                             << leaderID << " Actual: "
                             << result.consensusinfo().leaderid());
    return false;
  }

  ProtobufByteArrayToSerializable(result.consensusinfo().collectivesig(),
                                  collectiveSig);

  for (const auto& i : result.consensusinfo().bitmap()) {
    bitmap.emplace_back(i);
  }

  vector<unsigned char> tmp(result.consensusinfo().ByteSize());
  result.consensusinfo().SerializeToArray(tmp.data(), tmp.size());

  Signature signature;

  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (!Schnorr::GetInstance().Verify(tmp, signature, leaderKey)) {
    LOG_GENERAL(WARNING, "Invalid signature in collectivesig.");
    return false;
  }

  return true;
}

bool Messenger::SetConsensusCommitFailure(
    vector<unsigned char>& dst, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, const uint16_t backupID,
    const vector<unsigned char>& errorMsg,
    const pair<PrivKey, PubKey>& backupKey) {
  LOG_MARKER();

  ConsensusCommitFailure result;

  result.mutable_consensusinfo()->set_consensusid(consensusID);
  result.mutable_consensusinfo()->set_blocknumber(blockNumber);
  result.mutable_consensusinfo()->set_blockhash(blockHash.data(),
                                                blockHash.size());
  result.mutable_consensusinfo()->set_backupid(backupID);
  result.mutable_consensusinfo()->set_errormsg(errorMsg.data(),
                                               errorMsg.size());

  if (!result.consensusinfo().IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusCommitFailure.Data initialization failed.");
    return false;
  }

  vector<unsigned char> tmp(result.consensusinfo().ByteSize());
  result.consensusinfo().SerializeToArray(tmp.data(), tmp.size());

  Signature signature;

  if (!Schnorr::GetInstance().Sign(tmp, backupKey.first, backupKey.second,
                                   signature)) {
    LOG_GENERAL(WARNING, "Failed to sign commit failure.");
    return false;
  }

  SerializableToProtobufByteArray(signature, *result.mutable_signature());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusCommitFailure initialization failed.");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetConsensusCommitFailure(
    const vector<unsigned char>& src, const unsigned int offset,
    const uint32_t consensusID, const uint64_t blockNumber,
    const vector<unsigned char>& blockHash, uint16_t& backupID,
    vector<unsigned char>& errorMsg,
    const deque<pair<PubKey, Peer>>& committeeKeys) {
  LOG_MARKER();

  ConsensusCommitFailure result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ConsensusCommitFailure initialization failed.");
    return false;
  }

  if (result.consensusinfo().consensusid() != consensusID) {
    LOG_GENERAL(WARNING, "Consensus ID mismatch. Expected: "
                             << consensusID << " Actual: "
                             << result.consensusinfo().consensusid());
    return false;
  }

  if (result.consensusinfo().blocknumber() != blockNumber) {
    LOG_GENERAL(WARNING, "Block number mismatch. Expected: "
                             << blockNumber << " Actual: "
                             << result.consensusinfo().blocknumber());
    return false;
  }

  if ((result.consensusinfo().blockhash().size() != blockHash.size()) ||
      !equal(blockHash.begin(), blockHash.end(),
             result.consensusinfo().blockhash().begin(),
             [](const unsigned char left, const char right) -> bool {
               return left == (unsigned char)right;
             })) {
    std::vector<unsigned char> remoteBlockHash;
    remoteBlockHash.resize(result.consensusinfo().blockhash().size());
    std::copy(result.consensusinfo().blockhash().begin(),
              result.consensusinfo().blockhash().end(),
              remoteBlockHash.begin());
    LOG_GENERAL(WARNING,
                "Block hash mismatch. Expected: "
                    << DataConversion::Uint8VecToHexStr(blockHash)
                    << " Actual: "
                    << DataConversion::Uint8VecToHexStr(remoteBlockHash));
    return false;
  }

  backupID = result.consensusinfo().backupid();

  if (backupID >= committeeKeys.size()) {
    LOG_GENERAL(WARNING, "Backup ID beyond shard size. Backup ID: "
                             << backupID
                             << " Shard size: " << committeeKeys.size());
    return false;
  }

  errorMsg.resize(result.consensusinfo().errormsg().size());
  copy(result.consensusinfo().errormsg().begin(),
       result.consensusinfo().errormsg().end(), errorMsg.begin());

  vector<unsigned char> tmp(result.consensusinfo().ByteSize());
  result.consensusinfo().SerializeToArray(tmp.data(), tmp.size());

  Signature signature;

  ProtobufByteArrayToSerializable(result.signature(), signature);

  if (!Schnorr::GetInstance().Verify(tmp, signature,
                                     committeeKeys.at(backupID).first)) {
    LOG_GENERAL(WARNING, "Invalid signature in commit failure.");
    return false;
  }

  return true;
}

bool Messenger::SetBlockLink(
    std::vector<unsigned char>& dst, const unsigned int offset,
    const std::tuple<uint64_t, uint64_t, BlockType, BlockHash>& blocklink) {
  ProtoBlockLink result;

  result.set_index(get<BlockLinkIndex::INDEX>(blocklink));
  result.set_dsindex(get<BlockLinkIndex::DSINDEX>(blocklink));
  result.set_blocktype(get<BlockLinkIndex::BLOCKTYPE>(blocklink));
  BlockHash blkhash = get<BlockLinkIndex::BLOCKHASH>(blocklink);
  result.set_blockhash(blkhash.data(), blkhash.size);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "Failed to intialize ProtoBlockLink");
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetBlockLink(
    const vector<unsigned char>& src, const unsigned int offset,
    std::tuple<uint64_t, uint64_t, BlockType, BlockHash>& blocklink) {
  ProtoBlockLink result;
  BlockHash blkhash;
  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING, "ProtoBlockLink initialization failed");
    return false;
  }

  get<BlockLinkIndex::INDEX>(blocklink) = result.index();
  get<BlockLinkIndex::DSINDEX>(blocklink) = result.dsindex();
  copy(result.blockhash().begin(),
       result.blockhash().begin() + min((unsigned int)result.blockhash().size(),
                                        (unsigned int)blkhash.size),
       blkhash.asArray().begin());
  get<BlockLinkIndex::BLOCKTYPE>(blocklink) = (BlockType)result.blocktype();
  get<BlockLinkIndex::BLOCKHASH>(blocklink) = blkhash;

  return true;
}

bool Messenger::SetFallbackBlockWShardingStructure(
    vector<unsigned char>& dst, const unsigned int offset,
    const FallbackBlock& fallbackblock, const DequeOfShard& shards) {
  ProtoFallbackBlockWShardingStructure result;

  FallbackBlockToProtobuf(fallbackblock, *result.mutable_fallbackblock());
  ShardingStructureToProtobuf(shards, *result.mutable_sharding());

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING,
                "ProtoFallbackBlockWShardingStructure initialization failed");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetFallbackBlockWShardingStructure(
    const vector<unsigned char>& src, const unsigned int offset,
    FallbackBlock& fallbackblock, DequeOfShard& shards) {
  ProtoFallbackBlockWShardingStructure result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING,
                "ProtoFallbackBlockWShardingStructure initialization failed");
    return false;
  }

  ProtobufToFallbackBlock(result.fallbackblock(), fallbackblock);

  ProtobufToShardingStructure(result.sharding(), shards);

  return true;
}

bool Messenger::GetLookupGetDirectoryBlocksFromSeed(
    const vector<unsigned char>& src, const unsigned int offset,
    uint32_t& portno, uint64_t& index_num) {
  LookupGetDirectoryBlocksFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING,
                "LookupGetDirectoryBlocksFromSeed initialization failed");
    return false;
  }

  portno = result.portno();

  index_num = result.indexnum();

  return true;
}

bool Messenger::SetLookupGetDirectoryBlocksFromSeed(vector<unsigned char>& dst,
                                                    const unsigned int offset,
                                                    const uint32_t portno,
                                                    const uint64_t& index_num) {
  LookupGetDirectoryBlocksFromSeed result;

  result.set_portno(portno);
  result.set_indexnum(index_num);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING,
                "LookupGetDirectoryBlocksFromSeed initialization failed");
    return false;
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::SetLookupSetDirectoryBlocksFromSeed(
    vector<unsigned char>& dst, const unsigned int offset,
    const vector<
        boost::variant<DSBlock, VCBlock, FallbackBlockWShardingStructure>>&
        directoryBlocks,
    const uint64_t& index_num) {
  LookupSetDirectoryBlocksFromSeed result;

  result.set_indexnum(index_num);

  for (const auto& dirblock : directoryBlocks) {
    ProtoSingleDirectoryBlock* proto_dir_blocks = result.add_dirblocks();
    if (dirblock.type() == typeid(DSBlock)) {
      DSBlockToProtobuf(get<DSBlock>(dirblock),
                        *proto_dir_blocks->mutable_dsblock());
    } else if (dirblock.type() == typeid(VCBlock)) {
      VCBlockToProtobuf(get<VCBlock>(dirblock),
                        *proto_dir_blocks->mutable_vcblock());
    } else if (dirblock.type() == typeid(FallbackBlockWShardingStructure)) {
      FallbackBlockToProtobuf(
          get<FallbackBlockWShardingStructure>(dirblock).m_fallbackblock,
          *proto_dir_blocks->mutable_fallbackblockwshard()
               ->mutable_fallbackblock());
      ShardingStructureToProtobuf(
          get<FallbackBlockWShardingStructure>(dirblock).m_shards,
          *proto_dir_blocks->mutable_fallbackblockwshard()->mutable_sharding());
    }
  }

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING,
                "LookupSetDirectoryBlocksFromSeed initialization failed");
  }

  return SerializeToArray(result, dst, offset);
}

bool Messenger::GetLookupSetDirectoryBlocksFromSeed(
    const vector<unsigned char>& src, const unsigned int offset,
    vector<boost::variant<DSBlock, VCBlock, FallbackBlockWShardingStructure>>&
        directoryBlocks,
    uint64_t& index_num) {
  LookupSetDirectoryBlocksFromSeed result;

  result.ParseFromArray(src.data() + offset, src.size() - offset);

  if (!result.IsInitialized()) {
    LOG_GENERAL(WARNING,
                "LookupSetDirectoryBlocksFromSeed initialization failed");
    return false;
  }

  index_num = result.indexnum();

  for (const auto& dirblock : result.dirblocks()) {
    DSBlock dsblock;
    VCBlock vcblock;
    FallbackBlockWShardingStructure fallbackblockwshard;
    switch (dirblock.directoryblock_case()) {
      case ProtoSingleDirectoryBlock::DirectoryblockCase::kDsblock:
        if (!dirblock.dsblock().IsInitialized()) {
          LOG_GENERAL(WARNING, "DS block not initialized");
          continue;
        }
        ProtobufToDSBlock(dirblock.dsblock(), dsblock);
        directoryBlocks.emplace_back(dsblock);
        break;
      case ProtoSingleDirectoryBlock::DirectoryblockCase::kVcblock:
        if (!dirblock.vcblock().IsInitialized()) {
          LOG_GENERAL(WARNING, "VC block not initialized");
          continue;
        }
        ProtobufToVCBlock(dirblock.vcblock(), vcblock);
        directoryBlocks.emplace_back(vcblock);
        break;
      case ProtoSingleDirectoryBlock::DirectoryblockCase::kFallbackblockwshard:
        if (!dirblock.fallbackblockwshard().IsInitialized()) {
          LOG_GENERAL(WARNING, "FallbackBlock not initialized");
          continue;
        }
        ProtobufToFallbackBlock(dirblock.fallbackblockwshard().fallbackblock(),
                                fallbackblockwshard.m_fallbackblock);
        ProtobufToShardingStructure(dirblock.fallbackblockwshard().sharding(),
                                    fallbackblockwshard.m_shards);
        directoryBlocks.emplace_back(fallbackblockwshard);
        break;
      case ProtoSingleDirectoryBlock::DirectoryblockCase::
          DIRECTORYBLOCK_NOT_SET:
      default:
        LOG_GENERAL(WARNING, "Error in the blocktype");
        break;
    }
  }
  return true;
}