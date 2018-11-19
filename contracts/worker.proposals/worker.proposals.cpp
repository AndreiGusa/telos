#include "worker.proposals.hpp"
#include <eosiolib/print.hpp>
#include <ctime>

using namespace eosio;
using eosio::print;

workerproposal::workerproposal(account_name self) : contract(self), wp_env_singleton(self, self) {
    if (!wp_env_singleton.exists()) {

        wp_env_struct = wp_env{
            _self, //publisher
            2500000, // cycle duration in seconds (default 2,500,000 or 5,000,000 blocks or ~29 days)
            3, // percent from requested amount (default 3%)
            500000 // minimum fee amount (default 50 TLOS)
        };

        wp_env_singleton.set(wp_env_struct, _self);
    } else {
        wp_env_struct = wp_env_singleton.get();     
    }
}

workerproposal::~workerproposal() {
    
}

void workerproposal::submit(account_name proposer, std::string title, uint16_t cycles, std::string ipfs_location, asset amount, account_name receiver) {
    require_auth(proposer);

    // calc fee
    uint64_t fee_amount = uint64_t(amount.amount) * uint64_t( wp_env_struct.fee_percentage ) / uint64_t(100);
    fee_amount = fee_amount > wp_env_struct.fee_min ? fee_amount :  wp_env_struct.fee_min;

    // transfer the fee
    action(permission_level{ proposer, N(active) }, N(eosio.token), N(transfer), make_tuple(
    	proposer,
        N(eosio.saving),
        asset(int64_t(fee_amount), S(4, TLOS)),
        std::string("Worker Proposal Fee")
	)).send();

    proposals proptable(_self, _self);
    
    proptable.emplace( proposer, [&]( proposal& info ){
        info.id             = proptable.available_primary_key();
        info.ballot_id      = 0;
        info.proposer       = proposer;
        info.receiver       = receiver;
        info.title          = title;
        info.ipfs_location  = ipfs_location;
        info.cycles         = cycles;
        info.amount         = uint64_t(amount.amount);
        info.fee            = fee_amount;
        info.begin_time     = 0;
        info.end_time       = 0;
        info.status         = 0; //PROPOSAL_INACTIVE
        info.current_cycle  = 0;
    });

    print("\n PROPOSAL CREATED");
}

void workerproposal::linkballot(uint64_t prop_id, uint64_t ballot_id, account_name proposer) {
    require_auth(proposer);

    proposals proptable(_self, _self);
    auto p = proptable.find(prop_id);
    eosio_assert(p != proptable.end(), "Proposal with given id doesn't exist");
    auto prop = *p;

    ballots_table ballots(N(eosio.trail), N(eosio.trail));
    auto b = ballots.find(ballot_id);
    eosio_assert(b != ballots.end(), "Ballot with given id doesn't exist");
    auto bal = *b;

    eosio_assert(bal.publisher == proposer, "Cannot link to a ballot made by another account");

    proptable.modify(p, 0, [&]( auto& a ) {
        a.ballot_id     = bal.ballot_id;
        a.begin_time    = bal.begin_time;
        a.end_time      = bal.end_time;
        a.status        = 1; // ACTIVE
    });

    print("\nBallot Link: SUCCESS");
}

void workerproposal::claim(uint64_t prop_id, account_name proposer) {
    proposals proptable(_self, _self);
    auto p = proptable.find(prop_id);

    eosio_assert(p != proptable.end(), "Proposal Not Found");
    auto prop = *p;

    require_auth(proposer);

    ballots_table ballots(N(eosio.trail), N(eosio.trail));
    auto b = ballots.find(prop.ballot_id);
    eosio_assert(b != ballots.end(), "Ballot ID doesn't exist");
    auto bal = *b;

    eosio_assert(bal.end_time < now(), "Cycle is still open");
    
    eosio_assert(prop.status == 1, "Proposal is closed");

    environment_singleton environment(N(eosio.trail), N(eosio.trail));
    auto e = environment.get();

    asset outstanding = asset(0, S(4, TLOS));
    asset total_votes = (bal.yes_count + bal.no_count + bal.abstain_count); //total votes cast on proposal

    //pass thresholds
    uint64_t quorum_thresh = (e.total_voters / 10); // 10% of all registered voters

    //fee refund thresholds
    uint64_t q_fee_refund_thresh = (e.total_voters / 20); //0.1% of all TLOS tokens voting // 5% of voters for test (TODO: change to votes quorum)
    asset p_fee_refund_thresh = total_votes / 5; //20% of total votes
    
    if( prop.fee && bal.yes_count.amount > 0 && bal.yes_count >= p_fee_refund_thresh && bal.unique_voters >= q_fee_refund_thresh) {
        outstanding += asset(int64_t(prop.fee), S(4, TLOS));
        prop.fee = 0;
    }

    if(bal.yes_count > bal.no_count && bal.unique_voters >= quorum_thresh) {
        outstanding += asset(int64_t(prop.amount), S(4, TLOS));
    }

    if(outstanding.amount > 0) {
        action(permission_level{ _self, N(active) }, N(eosio.token), N(transfer), make_tuple(
            _self,
            prop.receiver,
            outstanding,
            std::string("Worker proposal funds")
        )).send();
    } else {
        print("\nNothing to claim from the last cycle");
    }

    if(prop.current_cycle == prop.cycles - 1) {
        prop.status = 0;
    }

    proptable.modify(p, 0, [&]( auto& a ) {
        a.status = prop.status;
        a.fee = prop.fee;
        a.current_cycle = prop.current_cycle + 1;
    });
}

EOSIO_ABI( workerproposal, (submit)(linkballot)(claim))
