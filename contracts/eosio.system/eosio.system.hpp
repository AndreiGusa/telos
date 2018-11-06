/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include <eosio.system/native.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/privileged.hpp>
#include <eosiolib/singleton.hpp>
#include <eosio.system/exchange_state.hpp>

#include <cmath>  
#include <string>

namespace eosiosystem {

   using eosio::asset;
   using eosio::indexed_by;
   using eosio::const_mem_fun;
   using eosio::block_timestamp;
   const uint32_t block_num_network_activation = 10800; // debug version 3600 blocks = 30 min
  //  const uint32_t block_num_network_activation = 1000000; 
   
   struct name_bid {
     account_name            newname;
     account_name            high_bidder;
     int64_t                 high_bid = 0; ///< negative high_bid == closed auction waiting to be claimed
     uint64_t                last_bid_time = 0;

     auto     primary_key()const { return newname;                          }
     uint64_t by_high_bid()const { return static_cast<uint64_t>(-high_bid); }
   };

   typedef eosio::multi_index< N(namebids), name_bid,
                               indexed_by<N(highbid), const_mem_fun<name_bid, uint64_t, &name_bid::by_high_bid>  >
                               >  name_bid_table;


   struct eosio_global_state : eosio::blockchain_parameters {
      uint64_t free_ram()const { return max_ram_size - total_ram_bytes_reserved; }

      uint64_t             max_ram_size = 12ll*1024 * 1024 * 1024;
      uint64_t             total_ram_bytes_reserved = 0;
      int64_t              total_ram_stake = 0;

      block_timestamp      last_producer_schedule_update;
      block_timestamp      last_proposed_schedule_update;
      uint64_t             last_pervote_bucket_fill = 0;
      int64_t              pervote_bucket = 0;
      int64_t              perblock_bucket = 0;
      uint32_t             total_unpaid_blocks = 0; /// all blocks which have been produced but not paid
      int64_t              total_activated_stake = 0;
      uint64_t             thresh_activated_stake_time = 0;
      uint16_t             last_producer_schedule_size = 0;
      double               total_producer_vote_weight = 0; /// the sum of all producer votes
      block_timestamp      last_name_close;
      uint32_t             block_num = 12;
      uint32_t             last_claimrewards = 0;
      uint32_t             next_payment = 0;

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE_DERIVED( eosio_global_state, eosio::blockchain_parameters,
                                (max_ram_size)(total_ram_bytes_reserved)(total_ram_stake)
                                (last_producer_schedule_update)(last_proposed_schedule_update)(last_pervote_bucket_fill)
                                (pervote_bucket)(perblock_bucket)(total_unpaid_blocks)(total_activated_stake)(thresh_activated_stake_time)
                                (last_producer_schedule_size)(total_producer_vote_weight)(last_name_close)(block_num)(last_claimrewards)(next_payment) )
   };

   enum class kick_type {
     REACHED_TRESHOLD = 1,
    //  PREVENT_LIB_STOP_MOVING = 2,
     BPS_VOTING = 2
   };
   /**
    * TELOS CHANGES:
    * 
    * 1. Added missed_blocks field, used for counting missed blocks.
    */
   struct producer_info {
      account_name          owner;
      double                total_votes = 0;
      eosio::public_key     producer_key; /// a packed public key object
      bool                  is_active = true;
      std::string           url;
      uint32_t              unpaid_blocks = 0;
      uint32_t              lifetime_unpaid_blocks = 0;
      uint32_t              missed_blocks_per_rotation = 0;
      uint32_t              lifetime_missed_blocks = 0;
      uint64_t              last_claim_time = 0;
      uint16_t              location = 0;
      
      uint32_t              kick_reason_id = 0;
      std::string           kick_reason;
      uint32_t              times_kicked = 0;
      uint32_t              kick_penalty_hours = 0; 
      block_timestamp       last_time_kicked;

      uint64_t primary_key()const { return owner;                                   }
      double   by_votes()const    { return is_active ? -total_votes : total_votes;  }
      bool     active()const      { return is_active;                               }
      void     deactivate()       { producer_key = public_key(); is_active = false; }
      
      void kick(kick_type kt, uint32_t penalty = 0) {
        times_kicked++;
        last_time_kicked = block_timestamp(eosio::time_point(eosio::microseconds(int64_t(current_time()))));
        
        if(penalty == 0) kick_penalty_hours  = uint32_t(std::pow(2, times_kicked));
        
        switch(kt) {
          case kick_type::REACHED_TRESHOLD:
            kick_reason_id = uint32_t(kick_type::REACHED_TRESHOLD);
            kick_reason = "Producer account was deactivated because it reached the maximum missed blocks in this rotation timeframe.";
          break;
          // case kick_type::PREVENT_LIB_STOP_MOVING:
          //   kick_reason_id = uint32_t(kick_type::PREVENT_LIB_STOP_MOVING);
          //   kick_reason = "Producer account was deactivated to prevent the LIB from halting.";
          // break;
          case kick_type::BPS_VOTING:
            kick_reason_id = uint32_t(kick_type::BPS_VOTING);
            kick_reason = "Producer account was deactivated by vote.";
            kick_penalty_hours = penalty;
          break;
        }
        
        deactivate();
      } 

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( producer_info, (owner)(total_votes)(producer_key)(is_active)(url)
                        (unpaid_blocks)(lifetime_unpaid_blocks)(missed_blocks_per_rotation)(lifetime_missed_blocks)(last_claim_time)
                        (location)(kick_reason_id)(kick_reason)(times_kicked)(kick_penalty_hours)(last_time_kicked) )
   };

   struct rotation_info {
      bool                            is_rotation_active = true;
      account_name                    bp_currently_out;
      account_name                    sbp_currently_in;
      uint32_t                        bp_out_index;
      uint32_t                        sbp_in_index;
      block_timestamp                 next_rotation_time;
      block_timestamp                 last_rotation_time;

      //NOTE: This might not be the best place for this information

      // bool                            is_kick_active = true;
      // account_name                    last_onblock_caller;
      // block_timestamp                 last_time_block_produced;
      
      EOSLIB_SERIALIZE( rotation_info, (is_rotation_active)(bp_currently_out)(sbp_currently_in)(bp_out_index)(sbp_in_index)(next_rotation_time)
                        (last_rotation_time)/*(is_kick_active)(last_onblock_caller)(last_time_block_produced)*/ )
   };

   struct voter_info {
      account_name                owner = 0; /// the voter
      account_name                proxy = 0; /// the proxy set by the voter, if any
      std::vector<account_name>   producers; /// the producers approved by this voter if no proxy set
      int64_t                     staked = 0;
      int64_t                 last_stake = 0;

      /**
       *  Every time a vote is cast we must first "undo" the last vote weight, before casting the
       *  new vote weight.  Vote weight is calculated as:
       *
       *  stated.amount * 2 ^ ( weeks_since_launch/weeks_per_year)
       */
      double                      last_vote_weight = 0; /// the vote weight cast the last time the vote was updated

      /**
       * Total vote weight delegated to this voter.
       */
      double                      proxied_vote_weight= 0; /// the total vote weight delegated to this voter as a proxy
      bool                        is_proxy = 0; /// whether the voter is a proxy for others


      uint32_t                    reserved1 = 0;
      time                        reserved2 = 0;
      eosio::asset                reserved3;

      uint64_t primary_key()const { return owner; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( voter_info, (owner)(proxy)(producers)(staked)(last_stake)(last_vote_weight)(proxied_vote_weight)(is_proxy)(reserved1)(reserved2)(reserved3) )
   };

   //tracks automated claimreward payments
   struct payment {
     account_name bp;
     asset pay;

     uint64_t primary_key() const { return bp; }
     EOSLIB_SERIALIZE(payment, (bp)(pay))
   };

   typedef eosio::multi_index<N(payments), payment> payments_table;

   typedef eosio::multi_index< N(voters), voter_info> voters_table;

   typedef eosio::singleton<N(schedulemetr), schedule_metrics> schedule_metrics_singleton;
   
   typedef eosio::singleton<N(rotations), rotation_info> rotation_info_singleton;

   typedef eosio::multi_index< N(producers), producer_info,
                               indexed_by<N(prototalvote), const_mem_fun<producer_info, double, &producer_info::by_votes>  >
                               >  producers_table;

   typedef eosio::singleton<N(global), eosio_global_state> global_state_singleton;

   //   static constexpr uint32_t     max_inflation_rate = 5;  // 5% annual inflation
   static constexpr uint32_t     seconds_per_day = 24 * 3600;
   static constexpr uint64_t     system_token_symbol = CORE_SYMBOL;

   class system_contract : public native {
      private:
         voters_table                 _voters;
         producers_table              _producers;
         global_state_singleton       _global;
         rotation_info_singleton      _rotations;
         schedule_metrics_singleton   _schedule_metrics; 

         eosio_global_state           _gstate;
         rotation_info                _grotations;
         rammarket                    _rammarket;
         payments_table               _payments;
         schedule_metrics             _gschedule_metrics;          

      public:
         system_contract( account_name s );
         ~system_contract();

         // Actions:
         void onblock( block_timestamp timestamp, account_name producer );
                      // const block_header& header ); /// only parse first 3 fields of block header

         // functions defined in delegate_bandwidth.cpp

         /**
          *  Stakes SYS from the balance of 'from' for the benfit of 'receiver'.
          *  If transfer == true, then 'receiver' can unstake to their account
          *  Else 'from' can unstake at any time.
          */
         void delegatebw( account_name from, account_name receiver,
                          asset stake_net_quantity, asset stake_cpu_quantity, bool transfer );


         /**
          *  Decreases the total tokens delegated by from to receiver and/or
          *  frees the memory associated with the delegation if there is nothing
          *  left to delegate.
          *
          *  This will cause an immediate reduction in net/cpu bandwidth of the
          *  receiver.
          *
          *  A transaction is scheduled to send the tokens back to 'from' after
          *  the staking period has passed. If existing transaction is scheduled, it
          *  will be canceled and a new transaction issued that has the combined
          *  undelegated amount.
          *
          *  The 'from' account loses voting power as a result of this call and
          *  all producer tallies are updated.
          */
         void undelegatebw( account_name from, account_name receiver,
                            asset unstake_net_quantity, asset unstake_cpu_quantity );


         /**
          * Increases receiver's ram quota based upon current price and quantity of
          * tokens provided. An inline transfer from receiver to system contract of
          * tokens will be executed.
          */
         void buyram( account_name buyer, account_name receiver, asset tokens );
         void buyrambytes( account_name buyer, account_name receiver, uint32_t bytes );

         /**
          *  Reduces quota my bytes and then performs an inline transfer of tokens
          *  to receiver based upon the average purchase price of the original quota.
          */
         void sellram( account_name receiver, int64_t bytes );

         /**
          *  This action is called after the delegation-period to claim all pending
          *  unstaked tokens belonging to owner
          */
         void refund( account_name owner );

         // functions defined in voting.cpp

         void regproducer( const account_name producer, const public_key& producer_key, const std::string& url, uint16_t location );

         void unregprod( const account_name producer );

         void setram( uint64_t max_ram_size );

         void voteproducer( const account_name voter, const account_name proxy, const std::vector<account_name>& producers );

         void regproxy( const account_name proxy, bool isproxy );

         void setparams( const eosio::blockchain_parameters& params );

         // functions defined in producer_pay.cpp
         void claimrewards( const account_name& owner );

         void claimrewards_snapshot();

         void setpriv( account_name account, uint8_t ispriv );

         void rmvproducer( account_name producer );

         void setkick(bool state);

         void setrotate(bool state);

         void bidname( account_name bidder, account_name newname, asset bid );
        
         void votebpout(account_name bp, uint32_t penalty_hours);
      private:
         
         void recalculate_votes();

         void update_rotation_time(block_timestamp block_time);

         void set_bps_rotation(account_name bpOut, account_name sbpIn);

         void update_elected_producers( block_timestamp timestamp );

         // Implementation details:

         //defind in delegate_bandwidth.cpp
         void changebw( account_name from, account_name receiver,
                        asset stake_net_quantity, asset stake_cpu_quantity, bool transfer );

         //defined in voting.hpp
         static eosio_global_state get_default_parameters();

         void update_votes( const account_name voter, const account_name proxy, const std::vector<account_name>& producers, bool voting );

         // defined in voting.cpp
         void propagate_weight_change( const voter_info& voter );

         //calculate the inverse vote weight
         double inverse_vote_weight(double staked, double amountVotedProducers);

         bool is_in_range(int32_t index, int32_t low_bound, int32_t up_bound);

         bool check_missed_blocks(block_timestamp timestamp, account_name producer);

         bool crossed_missed_blocks_threshold(uint32_t amountBlocksMissed, uint32_t schedule_size);

         void update_producer_missed_blocks(account_name producer);

         void update_missed_blocks_per_rotation();

         void restart_missed_blocks_per_rotation(std::vector<eosio::producer_key> prods);

         bool is_new_schedule_activated(account_name active_schedule[], uint32_t size);

         void reset_schedule_metrics(account_name producer);
   };

} /// eosiosystem
