/**
 * This file includes all definitions necessary to interact with Trail's voting system. Developers who want to
 * utilize the system simply must include this file in their implementation to interact with the information
 * stored by Trail.
 * 
 * @author Craig Branscom
 */

#include <eosiolib/eosio.hpp>
#include <eosiolib/permission.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/action.hpp>
#include <eosiolib/types.hpp>
#include <eosiolib/singleton.hpp>

using namespace std;
using namespace eosio;

#pragma region Structs

/// @abi table environment i64
struct environment {
    account_name publisher;
    uint64_t total_tokens;
    uint64_t total_voters;
    uint64_t total_ballots;

    uint64_t primary_key() const { return publisher; }
    EOSLIB_SERIALIZE(environment, (publisher)(total_tokens)(total_voters)(total_ballots))
};

///@abi table votereceipts i64
struct vote_receipt {
    uint64_t receipt_id;
    account_name voter;
    uint64_t vote_code;
    uint64_t vote_scope;
    uint64_t prop_id;
    uint16_t direction;
    asset weight;
    uint32_t expiration;

    uint64_t primary_key() const { return receipt_id; }
    uint64_t by_voter() const { return voter; }
    uint64_t by_code() const { return vote_code; }
    EOSLIB_SERIALIZE(vote_receipt, (receipt_id)(voter)(vote_code)(vote_scope)(prop_id)(direction)(weight)(expiration))
};

/// @abi table voters i64
struct voter_id {
    account_name voter;
    //vector<vote_receipt> receipt_list;

    uint64_t primary_key() const { return voter; }
    EOSLIB_SERIALIZE(voter_id, (voter))
};

/// @abi table ballots
struct ballot {
    account_name publisher;
    asset voting_tokens;

    uint64_t primary_key() const { return publisher; }
    //uint64_t by_sym() const { return voting_tokens.symbol.name(); }
    EOSLIB_SERIALIZE(ballot, (publisher)(voting_tokens))
};

struct vote_args {
    uint64_t vote_code;
    uint64_t vote_scope;
    uint64_t proposal_id;
    uint16_t direction;
    uint32_t expiration;
    account_name voter;
};

struct processvotes_args {
    uint64_t vote_code;
    uint64_t vote_scope;
    uint64_t proposal_id;
};

#pragma endregion Structs

#pragma region Tables

typedef singleton<N(environment), environment> environment_singleton;

typedef multi_index<N(voters), voter_id> voters_table;

typedef multi_index<N(ballots), ballot> ballots_table;

typedef multi_index<N(votereceipts), vote_receipt,
    indexed_by<N(bycode), const_mem_fun<vote_receipt, uint64_t, &vote_receipt::by_code>>,
    indexed_by<N(byvoter), const_mem_fun<vote_receipt, uint64_t, &vote_receipt::by_voter>>> receipts_table;

#pragma endregion Tables

#pragma region Helper_Functions

bool is_voter(account_name voter) {
    voters_table voters(N(eosio.trail), voter);
    auto v = voters.find(voter);

    if (v != voters.end()) {
        return true;
    }

    return false;
}

bool is_ballot(account_name publisher) {
    ballots_table ballots(N(eosio.trail), publisher);
    auto b = ballots.find(publisher);

    if (b != ballots.end()) {
        return true;
    }

    return false;
}

asset get_ballot_sym(account_name publisher) {
    ballots_table ballots(N(eosio.trail), publisher);
    auto b = ballots.get(publisher);

    return b.voting_tokens;
}

// receipts_table::const_iterator find_receipt(uint64_t r_code, uint64_t r_scope, uint64_t prop_id, symbol_name r_token, account_name voter) {
//     receipts_table votereceipts(N(eosio.trail), N(eosio.trail));
//     auto by_voter = votereceipts.get_index<N(byvoter)>();
//     auto itr = by_voter.lower_bound(voter);
//     while(itr->voter == voter) {
//         if (now() <= itr->expiration) {
//             return itr;
//         }
//         itr++;
//     }
//     return by_voter.end(); //returns the right itr? should be votereceipts.end()?
// }

#pragma endregion Helper_Functions
