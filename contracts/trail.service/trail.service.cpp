#include "trail.service.hpp"

trail::trail(account_name self) : contract(self), env_singleton(self, self) {
    if (!env_singleton.exists()) {

        env_struct = environment{
            self, //publisher
            0, //total_tokens
            0, //total_voters
            0 //total_ballots
        };

        env_singleton.set(env_struct, self);
    } else {
        env_struct = env_singleton.get();
    }
}

trail::~trail() {
    if (env_singleton.exists()) {
        env_singleton.set(env_struct, env_struct.publisher);
    }
}

#pragma region Tokens

void trail::regtoken(asset native, account_name publisher) {
    require_auth(publisher);

    auto sym = native.symbol.name();

    stats statstable(N(eosio.token), sym);
    auto eosio_existing = statstable.find(sym);
    eosio_assert(eosio_existing == statstable.end(), "Token with symbol already exists in eosio.token" );

    registries_table registries(_self, _self);
    auto r = registries.find(sym);
    eosio_assert(r == registries.end(), "Token Registry with that symbol already exists in Trail");

    //TODO: assert only 1 token per publisher

    registries.emplace(publisher, [&]( auto& a ){
        a.native = native;
        a.publisher = publisher;
    });

    env_struct.total_tokens++;

    print("\nToken Registration: SUCCESS");
}

void trail::unregtoken(asset native, account_name publisher) {
    require_auth(publisher);
    
    auto sym = native.symbol.name();
    registries_table registries(_self, _self);
    auto r = registries.find(sym);

    eosio_assert(r != registries.end(), "Token Registry does not exist.");

    registries.erase(r);

    env_struct.total_tokens--;

    print("\nToken Unregistration: SUCCESS");
}

#pragma endregion Trail_Tokens

#pragma region Voting

void trail::regvoter(account_name voter) {
    require_auth(voter);

    voters_table voters(_self, voter);
    auto v = voters.find(voter);

    eosio_assert(v == voters.end(), "Voter already exists");

    voters.emplace(voter, [&]( auto& a ){
        a.voter = voter;
    });

    env_struct.total_voters++;

    print("\nVoterID Registration: SUCCESS");
}

void trail::unregvoter(account_name voter) {
    require_auth(voter);

    voters_table voters(_self, voter);
    auto v = voters.find(voter);

    eosio_assert(v != voters.end(), "Voter Doesn't Exist");

    auto vid = *v;  

    voters.erase(v);

    env_struct.total_voters--;

    print("\nVoterID Unregistration: SUCCESS");
}

void trail::regballot(account_name publisher, asset voting_token) {
    require_auth(N(eosio.trail)); //NOTE: change to publisher to allow any account to register a ballot contract

    ballots_table ballots(_self, publisher);
    auto b = ballots.find(publisher);

    eosio_assert(b == ballots.end(), "Ballot already exists");

    ballots.emplace(_self, [&]( auto& a ){ //NOTE: change ram payer if allowing any account to register
        a.publisher = publisher;
        a.voting_tokens = voting_token;
    });

    env_struct.total_ballots++;

    print("\nBallot Registration: SUCCESS");
}

void trail::unregballot(account_name publisher) {
    require_auth(N(eosio.trail)); //NOTE: change to publisher to allow account to unregister itself

    ballots_table ballots(_self, publisher);
    auto b = ballots.find(publisher);

    eosio_assert(b != ballots.end(), "Ballot Doesn't Exist");

    ballots.erase(b);

    env_struct.total_ballots--;

    print("\nBallot Unregistration: SUCCESS");
}

#pragma endregion Voting

//EOSIO_ABI(trail, (regtoken)(unregtoken)(regvoter)(unregvoter)(addreceipt)(rmvexpvotes)(regballot)(unregballot))

extern "C" {
    void apply(uint64_t self, uint64_t code, uint64_t action) {
        trail _trail(self);
        if(code == self && action == N(regtoken)) {
            execute_action(&_trail, &trail::regtoken);
        } else if (code == self && action == N(unregtoken)) {
            execute_action(&_trail, &trail::unregtoken);
        } else if (code==self && action==N(regvoter)) {
            execute_action(&_trail, &trail::regvoter);
        } else if (code == self && action == N(unregvoter)) {
            execute_action(&_trail, &trail::unregvoter);
        } else if (code == self && action == N(regballot)) {
            execute_action(&_trail, &trail::regballot);
        } else if (code == self && action == N(unregballot)) {
            execute_action(&_trail, &trail::unregballot);
        } else if (code == N(eosio) && action == N(delegatebw)) {
            auto args = unpack_action_data<delegatebw_args>();
            asset new_weight = (args.stake_cpu_quantity + args.stake_net_quantity);

            receipts_table votereceipts(self, self);
            auto by_voter = votereceipts.get_index<N(byvoter)>();
            auto itr = by_voter.lower_bound(args.from);

            while(itr->voter == args.from) {
                if (now() <= itr->expiration && itr->weight.symbol.name() == new_weight.symbol.name()) {
                    by_voter.modify(itr, 0, [&]( auto& a ) {
                        a.weight += new_weight;
                    });
                    print("\npropagated weight change to id: ", itr->receipt_id);
                }
                itr++;
            }

        } else if (code == N(eosio) && action == N(undelegatebw)) {
            auto args = unpack_action_data<undelegatebw_args>();
            asset new_weight = (args.unstake_cpu_quantity + args.unstake_net_quantity);

            receipts_table votereceipts(self, self);
            auto by_voter = votereceipts.get_index<N(byvoter)>();
            auto itr = by_voter.lower_bound(args.from);

            while(itr->voter == args.from) {
                if (now() <= itr->expiration && itr->weight.symbol.name() == new_weight.symbol.name()) {
                    by_voter.modify(itr, 0, [&]( auto& a ) {
                        a.weight -= new_weight;
                    });
                    print("\npropagated weight change to id: ", itr->receipt_id);
                }
                itr++;
            }

        } else if (is_registry(code) && action == N(transfer)) {
            print("\ntransfer action received from: ", name{code});
            auto args = unpack_action_data<trail_transfer_args>();

            bool sender_is_voter = is_voter(args.sender);
            bool recip_is_voter = is_voter(args.recipient);

            if (!sender_is_voter && !recip_is_voter) { //if both false no need to continue
                return;
            }

            receipts_table votereceipts(self, self);
            auto by_voter = votereceipts.get_index<N(byvoter)>();
            auto itr = by_voter.lower_bound(args.sender);

            if (sender_is_voter) {
                while(itr->voter == args.sender) {
                    //NOTE: check if symbol.name() includes precision
                    if (now() <= itr->expiration && itr->weight.symbol.name() == args.tokens.symbol.name()) {
                        by_voter.modify(itr, 0, [&]( auto& a ) {
                            a.weight -= args.tokens;
                        });
                        print("\npropagated weight change to id: ", itr->receipt_id);
                    }
                    itr++;

                }
            }

            auto itr2 = by_voter.lower_bound(args.recipient);

            if (recip_is_voter) {
                while(itr2->voter == args.recipient) {
                    //NOTE: check if symbol.name() includes precision
                    if (now() <= itr2->expiration && itr2->weight.symbol.name() == args.tokens.symbol.name()) {
                        by_voter.modify(itr2, 0, [&]( auto& a ) {
                            a.weight += args.tokens;
                        });
                        print("\npropagated weight change to id: ", itr2->receipt_id);
                    }
                    itr2++;

                }
            }

            print("\ntoken balances propagated");

        } else if (is_ballot(code) && action == N(vote)) {
            print("\nvote action received from: ", name{code});
            auto args = unpack_action_data<vote_args>();
            
            asset new_weight;
            auto vt = get_ballot_sym(code);

            if (vt.symbol.name() == asset(0).symbol.name()) { //voting token is TLOS
                new_weight = get_staked_tlos(args.voter);
            } else if (is_trail_token(vt.symbol.name())) { //voting token is other token
                new_weight = get_token_balance(vt.symbol.name(), args.voter);
            } else {
                new_weight = asset(10000); //default weight of 1 TLOS?
            }

            receipts_table votereceipts(self, self);
            auto by_voter = votereceipts.get_index<N(byvoter)>();
            auto itr = by_voter.lower_bound(args.voter);

            if (itr == by_voter.end()) {

                votereceipts.emplace(self, [&]( auto& a ){
                    a.receipt_id = votereceipts.available_primary_key();
                    a.voter = args.voter;
                    a.vote_code = args.vote_code;
                    a.vote_scope = args.vote_scope;
                    a.prop_id = args.proposal_id;
                    a.direction = args.direction;
                    a.weight = new_weight;
                    a.expiration = args.expiration;
                });

                print("\nfirst votereceipt emplacement complete");
            } else {

                bool found = false;
                while(itr->voter == args.voter) {
                    print("\nchecking vr...");
                    if (now() <= itr->expiration && 
                        itr->vote_code == args.vote_code && 
                        itr->vote_scope == args.vote_scope && 
                        itr->prop_id == args.proposal_id) {

                        by_voter.modify(itr, 0, [&]( auto& a ) {
                            a.direction = args.direction;
                            a.weight = new_weight;
                        });

                        found = true;
                        print("\nVR found and updated");
                        break;
                    }
                    itr++;
                }

                if (!found) {
                    votereceipts.emplace(self, [&]( auto& a ){
                        a.receipt_id = votereceipts.available_primary_key();
                        a.voter = args.voter;
                        a.vote_code = args.vote_code;
                        a.vote_scope = args.vote_scope;
                        a.prop_id = args.proposal_id;
                        a.direction = args.direction;
                        a.weight = new_weight;
                        a.expiration = args.expiration;
                    });

                    print("\nnew vr emplaced");
                }

                print("\n\nvotereceipt changes complete");
            }

        } else if (code == N(eosio.amend) && action == N(processvotes)) { //TODO: change to is_ballot()
            auto args = unpack_action_data<processvotes_args>();
            receipts_table votereceipts(self, self);
            auto by_code = votereceipts.get_index<N(bycode)>();
            auto itr = by_code.lower_bound(args.vote_code);

            eosio_assert(itr != by_code.end(), "no votes to process");
            print("\nTrail beginning vr search...");

            uint64_t loops = 0;
            int64_t new_no_votes = 0;
            int64_t new_yes_votes = 0;
            int64_t new_abs_votes = 0;

            print("\nlower bound id: ", itr->receipt_id);

            while(itr->vote_code == args.vote_code && loops < args.loop_count) { //loops variable to limit cpu/net expense per call
                
                if (itr->vote_scope == args.vote_scope &&
                    itr->prop_id == args.proposal_id &&
                    now() > itr->expiration) {

                    switch (itr->direction) { //NOTE: cast as voted asset
                        case 0 : new_no_votes += itr->weight.amount; break;
                        case 1 : new_yes_votes += itr->weight.amount; break;
                        case 2 : new_abs_votes += itr->weight.amount; break;
                    }

                    itr = by_code.erase(itr);

                } else {
                    itr++;
                }

                loops++;
            }

            print("\nloops processed: ", loops);
            print("\nnew no votes: ", asset(new_no_votes));
            print("\nnew yes votes: ", asset(new_yes_votes));
            print("\nnew abstain votes: ", asset(new_abs_votes));

        }
    } //end apply
};
