#include "ratify.amend.hpp"

#include <eosiolib/print.hpp>

ratifyamend::ratifyamend(account_name self) : contract(self), thresh_singleton(self, self) {
    if (!thresh_singleton.exists()) {

        thresh_struct = threshold{
            _self, //publisher
            0, //initial total_voters
            0, //initial quorum_threshold
            uint32_t(5000) //expiration_length in seconds (default is 5,000,000 or ~58 days)
        };

        update_thresh();
        thresh_singleton.set(thresh_struct, _self);
    } else {
        thresh_struct = thresh_singleton.get();     
        update_thresh();
        //thresh_singleton.set(thresh_struct, _self);
    }
}

ratifyamend::~ratifyamend() {
    if (thresh_singleton.exists()) {
        thresh_singleton.set(thresh_struct, _self);
    }
}

void ratifyamend::insertdoc(string title, vector<string> clauses) {
    require_auth(_self); //only contract owner can insert new document
    
    documents_table documents(_self, _self);

    uint64_t doc_id = documents.available_primary_key();

    documents.emplace(_self, [&]( auto& a ){
        a.id = doc_id;
        a.title = title;
        a.clauses = clauses;
    });

    print("\nDocument Insertion: SUCCESS");
    print("\nAssigned Document ID: ", doc_id);
}

void ratifyamend::propose(string title, uint64_t document_id, vector<uint16_t> new_clause_ids, vector<string> new_ipfs_urls, account_name proposer) {
    require_auth(proposer);

    documents_table documents(_self, _self);
    auto d = documents.find(document_id);
    eosio_assert(d != documents.end(), "Document Not Found");
    auto doc_struct = *d;

    eosio_assert(new_clause_ids.size() == new_ipfs_urls.size(), "Clause ID vector and IPFS url vector sizes must match");

    auto doc_size = doc_struct.clauses.size();
    int16_t last_clause_id = -1;
    auto last_ipfs_url = 0;

    for (int i = 0; i < new_clause_ids.size(); i++) {
        eosio_assert(new_clause_ids.at(i) > last_clause_id, "Clause IDs Must Be In Ascending Order");
        last_clause_id = new_clause_ids.at(i);
        eosio_assert(new_clause_ids.at(i) <= (doc_size + 1), "Invalid Clause Number");
        
        last_ipfs_url++;
        if (new_clause_ids.at(i) == (doc_size + 1)) { //if clause is new, increase doc_size
            doc_size++;
        }
    }

    //NOTE: 100.0000 TLOS fee, refunded if proposal passes or meets specified lowered thresholds
    action(permission_level{ proposer, N(active) }, N(eosio.token), N(transfer), make_tuple(
    	proposer,
        _self,
        asset(int64_t(1000000), S(4, TLOS)),
        std::string("Ratify/Amend Proposal Fee")
	)).send();

    proposals_table proposals(_self, _self);

    proposals.emplace(proposer, [&]( auto& a ){
        a.id = proposals.available_primary_key();
        a.document_id = document_id;
        a.title = title;
        a.new_clause_ids = new_clause_ids;
        a.new_ipfs_urls = new_ipfs_urls;
        a.yes_count = asset(0);
        a.no_count = asset(0);
        a.abstain_count = asset(0);
        a.total_voters = 0;
        a.proposer = proposer;
        a.vote_code = _self;
        a.vote_scope = _self;
        a.expiration = now() + thresh_struct.expiration_length;
        a.status = 0;
    });
}

void ratifyamend::vote(uint64_t vote_code, uint64_t vote_scope, uint64_t proposal_id, uint16_t direction, uint32_t expiration, account_name voter) {
    require_auth(voter);
    eosio_assert(direction >= 0 && direction <= 2, "Invalid Vote. [0 = NO, 1 = YES, 2 = ABSTAIN]");
    eosio_assert(is_voter(voter), "voter is not registered");

    proposals_table proposals(_self, _self);
    auto p = proposals.find(proposal_id);
    eosio_assert(p != proposals.end(), "Proposal Not Found");
    auto prop = *p;
    eosio_assert(prop.expiration >= now(), "Proposal Has Expired");

    eosio_assert(expiration == prop.expiration, "expiration does not match proposal");
    eosio_assert(vote_code == _self, "vote_code must be eosio.amend");
    eosio_assert(vote_scope == _self, "vote_scope must be eosio.amend");

    require_recipient(N(eosio.trail));
    print("\nVote sent to Trail");
}

void ratifyamend::processvotes(uint64_t vote_code, uint64_t vote_scope, uint64_t proposal_id, uint16_t loop_count) {
    proposals_table proposals(_self, _self);
    auto p = proposals.find(proposal_id);
    eosio_assert(p != proposals.end(), "Proposal Not Found");
    auto prop = *p;
    eosio_assert(prop.expiration < now(), "Proposal is still open");
    
    receipts_table votereceipts(N(eosio.trail), N(eosio.trail));
    auto by_code = votereceipts.get_index<N(bycode)>();
    auto itr = by_code.lower_bound(vote_code);

    eosio_assert(itr != by_code.end(), "no votes to process");
    print("\neosio.amend processing votes...");

    uint64_t loops = 0;
    uint64_t unique_voters = 0;
    int64_t new_no_votes = 0;
    int64_t new_yes_votes = 0;
    int64_t new_abs_votes = 0;

    while(itr->vote_code == vote_code && loops < loop_count) { //loops variable to limit cpu/net expense per call
        
        if (itr->vote_scope == vote_scope &&
            itr->prop_id == proposal_id &&
            now() > itr->expiration) {

            print("\nvr found...counting...");
            
            switch (itr->direction) {
                case 0 : new_no_votes += itr->weight.amount; break;
                case 1 : new_yes_votes += itr->weight.amount; break;
                case 2 : new_abs_votes += itr->weight.amount; break;
            }

            unique_voters++;
        }
        loops++;
        itr++;
    }

    proposals.modify(p, 0, [&]( auto& a ) {
        a.no_count += asset(new_no_votes);
        a.yes_count += asset(new_yes_votes);
        a.abstain_count += asset(new_abs_votes);
        a.total_voters += unique_voters;
    });

    print("\nloops processed: ", loops);
    print("\nnew no votes: ", asset(new_no_votes));
    print("\nnew yes votes: ", asset(new_yes_votes));
    print("\nnew abstain votes: ", asset(new_abs_votes));

    require_recipient(N(eosio.trail));
}

void ratifyamend::close(uint64_t proposal_id) {
    proposals_table proposals(_self, _self);
    auto p = proposals.find(proposal_id);
    eosio_assert(p != proposals.end(), "Proposal Not Found");
    auto prop = *p;
    eosio_assert(prop.expiration < now(), "Proposal is still open");
    eosio_assert(prop.status == 0, "Proposal is already closed");

    receipts_table votereceipts(N(eosio.trail), N(eosio.trail));
    auto by_code = votereceipts.get_index<N(bycode)>();
    auto itr = by_code.lower_bound(_self);

    eosio_assert(itr == by_code.end(), "proposal still has open vote receipts to process");
    print("\nno votes to process...closing proposal and rendering verdict");

    asset total_votes = (prop.yes_count + prop.no_count + prop.abstain_count); //total votes cast on proposal

    //pass thresholds
    uint64_t quorum_thresh = (thresh_struct.total_voters / 20); // 5% of all registered voters //TODO: update percentage with real value
    asset pass_thresh = ((prop.yes_count + prop.no_count) / 3) * 2; // 66.67% yes votes of total_votes

    //refund thresholds - both must be met for a refund - proposal pass triggers automatic refund
    uint64_t q_refund_thresh = (thresh_struct.total_voters / 25); //4% of all registered voters
    asset p_refund_thresh = total_votes / 4; //25% yes votes of total_votes

    if (prop.total_voters >= quorum_thresh && total_votes >= pass_thresh) {

        //proposal passed, refund granted

        proposals.modify(p, 0, [&]( auto& a ) {
            a.status = 1;
        });

        action(permission_level{ _self, N(active) }, N(eosio.token), N(transfer), make_tuple(
            _self,
            prop.proposer,
            asset(int64_t(1000000), S(4, TLOS)),
            std::string("Ratify/Amend Proposal Fee Refund")
        )).send();

        update_doc(prop.document_id, prop.new_clause_ids, prop.new_ipfs_urls);

    } else if (prop.total_voters >= q_refund_thresh && total_votes >= p_refund_thresh) {
        
        //proposal failed, refund granted

        proposals.modify(p, 0, [&]( auto& a ) {
            a.status = 2;
        });

        action(permission_level{ _self, N(active) }, N(eosio.token), N(transfer), make_tuple(
            _self,
            prop.proposer,
            asset(int64_t(1000000), S(4, TLOS)),
            std::string("Ratify/Amend Proposal Fee Refund")
        )).send();
        
    } else {
        
        //proposal failed, refund witheld

        proposals.modify(p, 0, [&]( auto& a ) {
            a.status = 2;
        });

        print("\nproposal refund witheld");
    }

}

#pragma region Helper_Functions

void ratifyamend::update_thresh() {

    environment_singleton env(N(eosio.trail), N(eosio.trail));
    environment e = env.get();

    uint64_t new_quorum = e.total_voters / 4; //25% of all registered voters
    uint64_t new_total_voters = e.total_voters;

    thresh_struct.quorum_threshold = new_quorum;
    thresh_struct.total_voters = new_total_voters;
}

void ratifyamend::update_doc(uint64_t document_id, vector<uint16_t> new_clause_ids, vector<string> new_ipfs_urls) {
    documents_table documents(_self, _self);
    auto d = documents.find(document_id);
    auto doc = *d;

    auto doc_size = doc.clauses.size();
    for (int i = 0; i < new_clause_ids.size(); i++) {
        
        if (new_clause_ids[i] < doc.clauses.size()) { //update existing clause
            doc.clauses[new_clause_ids[i]] = new_ipfs_urls.at(i);
        } else { //add new clause
            doc.clauses.push_back(new_ipfs_urls.at(i));
        }
    }

    documents.modify(d, 0, [&]( auto& a ) {
        a.clauses = doc.clauses;
    });
}

#pragma endregion Helper_Functions

EOSIO_ABI(ratifyamend, (insertdoc)(propose)(vote)(processvotes)(close))
