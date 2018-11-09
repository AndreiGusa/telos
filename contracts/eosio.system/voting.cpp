/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include "eosio.system.hpp"

#include <eosiolib/eosio.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/print.hpp>
#include <eosiolib/datastream.hpp>
#include <eosiolib/serialize.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/privileged.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/transaction.hpp>
#include <eosio.token/eosio.token.hpp>
#include <algorithm>
#include <cmath>


#define TWELVE_HOURS_US  43200000000
#define SIX_HOURS_US     21600000000
#define ONE_HOUR_US        900000000 // 15 min ***quick test
#define SIX_MINUTES_US     360000000 // debug version
#define TWELVE_MINUTES_US  720000000
#define MAX_PRODUCERS             51
#define TOP_PRODUCERS             21
#define MAX_VOTE_PRODUCERS        30

namespace eosiosystem {
   using namespace eosio;
   using eosio::indexed_by;
   using eosio::const_mem_fun;
   using eosio::bytes;
   using eosio::print;
   using eosio::singleton;
   using eosio::transaction;
   
   /**
    *  This method will create a producer_config and producer_info object for 'producer'
    *
    *  @pre producer is not already registered
    *  @pre producer to register is an account
    *  @pre authority of producer to register
    *
    */
   void system_contract::regproducer( const account_name producer, const eosio::public_key& producer_key, const std::string& url, uint16_t location ) {
      eosio_assert( url.size() < 512, "url too long" );
      eosio_assert( producer_key != eosio::public_key(), "public key should not be the default value" );
      require_auth( producer );

      auto prod = _producers.find( producer );
      if ( prod != _producers.end() ) {
        _producers.modify(prod, producer, [&](producer_info &info) {
          auto now = block_timestamp(eosio::time_point(eosio::microseconds(int64_t(current_time()))));

          block_timestamp penalty_expiration_time = block_timestamp(info.last_time_kicked.to_time_point() + time_point(hours(int64_t(info.kick_penalty_hours))));
          
          eosio_assert(now.slot > penalty_expiration_time.slot,
            std::string("Producer is not allowed to register at this time. Please fix your node and try again later in: " 
            + std::to_string( uint32_t((penalty_expiration_time.slot - now.slot) / 2 ))  
            + " seconds").c_str());
            
          info.producer_key = producer_key;
          info.url = url;
          info.location = location;
          info.is_active = true;
        });
      } else {
        _producers.emplace(producer, [&](producer_info &info) {
          info.owner = producer;
          info.total_votes = 0;
          info.producer_key = producer_key;
          info.is_active = true;
          info.url = url;
          info.location = location;
        });
      }
   }

   void system_contract::unregprod( const account_name producer ) {
      require_auth( producer );

      const auto& prod = _producers.get( producer, "producer not found" );

      _producers.modify( prod, 0, [&]( producer_info& info ) {
          info.deactivate();
      });
   }

   void system_contract::set_bps_rotation(account_name bpOut, account_name sbpIn) {
      _grotations.bp_currently_out = bpOut;
      _grotations.sbp_currently_in = sbpIn;
   }

   void system_contract::update_rotation_time(block_timestamp block_time) {
      _grotations.last_rotation_time = block_time;
      _grotations.next_rotation_time = block_timestamp(block_time.to_time_point() + time_point(microseconds(TWELVE_HOURS_US)));
   }

   void system_contract::restart_missed_blocks_per_rotation(std::vector<eosio::producer_key> prods) {
        // restart all missed blocks to bps and sbps
        for (size_t i = 0; i < prods.size(); i++) {
          auto bp_name = prods[i].producer_name;
          auto pitr = _producers.find(bp_name);

          if (pitr != _producers.end()) {
            _producers.modify(pitr, 0, [&](auto &p) {
              if (p.times_kicked > 0 && p.missed_blocks_per_rotation == 0) {
                p.times_kicked--;
              } 
              p.lifetime_missed_blocks += p.missed_blocks_per_rotation;
              p.missed_blocks_per_rotation = 0;
            });
          }
        }
   }

   //TODO: Add _grotations.is_rotation_active, that way this feature can be toggled.
   void system_contract::update_elected_producers( block_timestamp block_time ) {
      _gstate.last_producer_schedule_update = block_time;

      auto idx = _producers.get_index<N(prototalvote)>();

      uint32_t totalActiveVotedProds = uint32_t(std::distance(idx.begin(), idx.end()));
      totalActiveVotedProds = totalActiveVotedProds > MAX_PRODUCERS ? MAX_PRODUCERS : totalActiveVotedProds;
      
      std::vector<eosio::producer_key> prods;
      prods.reserve(size_t(totalActiveVotedProds));

      //add active producers with vote > 0
      for ( auto it = idx.cbegin(); it != idx.cend() && prods.size() < totalActiveVotedProds && it->total_votes > 0 && it->active(); ++it ) {
         prods.emplace_back( eosio::producer_key{it->owner, it->producer_key} );
      }

      totalActiveVotedProds = prods.size();
      
      vector<eosio::producer_key>::iterator it_bp = prods.end();
      vector<eosio::producer_key>::iterator it_sbp = prods.end();

      if (_grotations.next_rotation_time <= block_time) {
        
        if (totalActiveVotedProds > TOP_PRODUCERS) {
          _grotations.bp_out_index = _grotations.bp_out_index >= TOP_PRODUCERS - 1 ? 0 : _grotations.bp_out_index + 1;
          _grotations.sbp_in_index = _grotations.sbp_in_index >= totalActiveVotedProds - 1 ? TOP_PRODUCERS : _grotations.sbp_in_index + 1;

          account_name bp_name = prods[_grotations.bp_out_index].producer_name;
          account_name sbp_name = prods[_grotations.sbp_in_index].producer_name;

          it_bp = prods.begin() + int32_t(_grotations.bp_out_index);
          it_sbp = prods.begin() + int32_t(_grotations.sbp_in_index);

          set_bps_rotation(bp_name, sbp_name);
        } 

        update_rotation_time(block_time);
        restart_missed_blocks_per_rotation(prods);
      }
      else {
        if(_grotations.bp_currently_out != 0 && _grotations.sbp_currently_in != 0) {
          auto bp_name = _grotations.bp_currently_out;
          it_bp = std::find_if(prods.begin(), prods.end(), [&bp_name](const eosio::producer_key &g) {
            return g.producer_name == bp_name; 
          });

          auto sbp_name = _grotations.sbp_currently_in;
          it_sbp = std::find_if(prods.begin(), prods.end(), [&sbp_name](const eosio::producer_key &g) {
            return g.producer_name == sbp_name; 
          });
          auto _bp_index = std::distance(prods.begin(), it_bp);
          auto _sbp_index = std::distance(prods.begin(), it_sbp);

          if(it_bp == prods.end() || it_sbp == prods.end()) {
              set_bps_rotation(0, 0);

            if(totalActiveVotedProds < TOP_PRODUCERS) {
              _grotations.bp_out_index = TOP_PRODUCERS;
              _grotations.sbp_in_index = MAX_PRODUCERS+1;
            }
          } else if (totalActiveVotedProds > TOP_PRODUCERS && (!is_in_range(_bp_index, 0, TOP_PRODUCERS) || !is_in_range(_sbp_index, TOP_PRODUCERS, MAX_PRODUCERS))) {
              set_bps_rotation(0, 0);
              it_bp = prods.end();
              it_sbp = prods.end();
          }
        }
    }

      std::vector<eosio::producer_key>  top_producers;
      
      //Rotation
      if(it_bp != prods.end() && it_sbp != prods.end()) {
        for ( auto pIt = prods.begin(); pIt != prods.end(); ++pIt) {
          auto i = std::distance(prods.begin(), pIt); 
          // print("\ni-> ", i);
          if(i > TOP_PRODUCERS - 1) break;

          if(pIt->producer_name == it_bp->producer_name) {
            // print("\nprod sbp added to schedule -> ", name{it_sbp->producer_name});
            top_producers.emplace_back(*it_sbp);
          } else {
            // print("\nprod bp added to schedule -> ", name{pIt->producer_name});
            top_producers.emplace_back(*pIt);
          } 
        }
      } 
      else {
        top_producers = prods;
        if(prods.size() > TOP_PRODUCERS) top_producers.resize(TOP_PRODUCERS);
        else top_producers.resize(prods.size());
      }

      // if ( top_producers.size() < _gstate.last_producer_schedule_size ) {
      //    return;
      // }

      // sort by producer name
      std::sort( top_producers.begin(), top_producers.end() );
      bytes packed_schedule = pack(top_producers);

      auto schedule_version = set_proposed_producers( packed_schedule.data(),  packed_schedule.size());
      if (schedule_version >= 0) {
        _gstate.last_proposed_schedule_update = block_time;
        _gschedule_metrics.producers_metric.erase(_gschedule_metrics.producers_metric.begin(), _gschedule_metrics.producers_metric.end());
       
        print("\n**new schedule was proposed**");
        std::vector<producer_metric> psm;
        std::for_each(top_producers.begin(), top_producers.end(), [&psm](auto &tp) {
          auto bp_name = tp.producer_name;
          psm.emplace_back(producer_metric{ bp_name, 12 });
        });
 
        _gschedule_metrics.producers_metric = psm;
        
        _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>(top_producers.size());
      }
   }
   
   /*
   * This function caculates the inverse weight voting. 
   * The maximum weighted vote will be reached if an account votes for the maximum number of registered producers (up to 30 in total).  
   */   
   double system_contract::inverse_vote_weight(double staked, double amountVotedProducers) {
     if (amountVotedProducers == 0.0) {
       return 0;
     }

     double percentVoted = amountVotedProducers / MAX_VOTE_PRODUCERS;
     double voteWeight = (sin(M_PI * percentVoted - M_PI_2) + 1.0) / 2.0;
     return (voteWeight * staked);
   }


   bool system_contract::is_in_range(int32_t index, int32_t low_bound, int32_t up_bound) {
     return index >= low_bound && index < up_bound;
   } 

   /**
    *  @pre producers must be sorted from lowest to highest and must be registered and active
    *  @pre if proxy is set then no producers can be voted for
    *  @pre if proxy is set then proxy account must exist and be registered as a proxy
    *  @pre every listed producer or proxy must have been previously registered
    *  @pre voter must authorize this action
    *  @pre voter must have previously staked some EOS for voting
    *  @pre voter->staked must be up to date
    *
    *  @post every producer previously voted for will have vote reduced by previous vote weight
    *  @post every producer newly voted for will have vote increased by new vote amount
    *  @post prior proxy will proxied_vote_weight decremented by previous vote weight
    *  @post new proxy will proxied_vote_weight incremented by new vote weight
    *
    *  If voting for a proxy, the producer votes will not change until the proxy updates their own vote.
    */
   void system_contract::voteproducer(const account_name voter_name, const account_name proxy, const std::vector<account_name> &producers) {
     require_auth(voter_name);
     update_votes(voter_name, proxy, producers, true);
   }
   
   void system_contract::update_votes( const account_name voter_name, const account_name proxy, const std::vector<account_name>& producers, bool voting ) {
      //validate input
      if ( proxy ) {
         eosio_assert( producers.size() == 0, "cannot vote for producers and proxy at same time" );
         eosio_assert( voter_name != proxy, "cannot proxy to self" );
         require_recipient( proxy );
      } else {
         eosio_assert( producers.size() <= MAX_VOTE_PRODUCERS, "attempt to vote for too many producers" );
         for( size_t i = 1; i < producers.size(); ++i ) {
            eosio_assert( producers[i-1] < producers[i], "producer votes must be unique and sorted" );
         }
      }

      auto voter = _voters.find(voter_name);
      eosio_assert( voter != _voters.end(), "user must stake before they can vote" ); /// staking creates voter object
      eosio_assert( !proxy || !voter->is_proxy, "account registered as a proxy is not allowed to use a proxy" );

      auto totalStaked = voter->staked;
      if(voter->is_proxy){
         totalStaked += voter->proxied_vote_weight;
      }

      // when unvoting, set the stake used for calculations to 0
      // since it is the equivalent to retracting your stake
      if(voting && !proxy && producers.size() == 0){
         totalStaked = 0;
      }

      // when a voter or a proxy votes or changes stake, the total_activated stake should be re-calculated
      // any proxy stake handling should be done when the proxy votes or on weight propagation
      // if(_gstate.thresh_activated_stake_time == 0 && !proxy && !voter->proxy){
      if(!proxy && !voter->proxy){
         _gstate.total_activated_stake += totalStaked - voter->last_stake;
      }

      auto new_vote_weight = inverse_vote_weight((double )totalStaked, (double) producers.size());
      boost::container::flat_map<account_name, pair<double, bool /*new*/> > producer_deltas;

      // print("\n Voter : ", voter->last_stake, " = ", voter->last_vote_weight, " = ", proxy, " = ", producers.size(), " = ", totalStaked, " = ", new_vote_weight);
      
      //Voter from second vote
      if ( voter->last_stake > 0 ) {

         //if voter account has set proxy to another voter account
         if( voter->proxy ) { 
            auto old_proxy = _voters.find( voter->proxy );

            eosio_assert( old_proxy != _voters.end(), "old proxy not found" ); //data corruption
               
            _voters.modify( old_proxy, 0, [&]( auto& vp ) {
               vp.proxied_vote_weight -= voter->last_stake;
            });

            // propagate weight here only when switching proxies
            // otherwise propagate happens in the case below
            if( proxy != voter->proxy ) {  
               _gstate.total_activated_stake += totalStaked - voter->last_stake;
               propagate_weight_change( *old_proxy );
            }
         } else {
            for( const auto& p : voter->producers ) {
               auto& d = producer_deltas[p];
               d.first -= voter->last_vote_weight;
               d.second = false;
            }
         }
      }

      if( proxy ) {
         auto new_proxy = _voters.find( proxy );
         eosio_assert( new_proxy != _voters.end(), "invalid proxy specified" ); //if ( !voting ) { data corruption } else { wrong vote }
         eosio_assert( !voting || new_proxy->is_proxy, "proxy not found" );
        
         _voters.modify( new_proxy, 0, [&]( auto& vp ) {
            vp.proxied_vote_weight += voter->staked;
         });
         
         if((*new_proxy).last_vote_weight > 0){
            _gstate.total_activated_stake += totalStaked - voter->last_stake;
            propagate_weight_change( *new_proxy );
         }
      } else {
         if( new_vote_weight >= 0 ) {
            for( const auto& p : producers ) {
               auto& d = producer_deltas[p]; 
               d.first += new_vote_weight;
               d.second = true;
            }
         }
      }

      for( const auto& pd : producer_deltas ) {
         auto pitr = _producers.find( pd.first );
         if( pitr != _producers.end() ) {
            eosio_assert( !voting || pitr->active() || !pd.second.second /* not from new set */, "producer is not currently registered" );
            _producers.modify( pitr, 0, [&]( auto& p ) {
               p.total_votes += pd.second.first;
               if ( p.total_votes < 0 ) { // floating point arithmetics can give small negative numbers
                  p.total_votes = 0;
               }
               _gstate.total_producer_vote_weight += pd.second.first;
            });
         } else {
            eosio_assert( !pd.second.second /* not from new set */, "producer is not registered" ); //data corruption
         }
      }

      _voters.modify( voter, 0, [&]( auto& av ) {
         av.last_vote_weight = new_vote_weight;
         av.last_stake = int64_t(totalStaked);
         av.producers = producers;
         av.proxy     = proxy;
      });
   }

   /**
    *  An account marked as a proxy can vote with the weight of other accounts which
    *  have selected it as a proxy. Other accounts must refresh their voteproducer to
    *  update the proxy's weight.
    *
    *  @param isproxy - true if proxy wishes to vote on behalf of others, false otherwise
    *  @pre proxy must have something staked (existing row in voters table)
    *  @pre new state must be different than current state
    */
   void system_contract::regproxy( const account_name proxy, bool isproxy ) {
      require_auth( proxy );
      auto pitr = _voters.find(proxy);
      if ( pitr != _voters.end() ) {
         eosio_assert( isproxy != pitr->is_proxy, "action has no effect" );
         eosio_assert( !isproxy || !pitr->proxy, "account that uses a proxy is not allowed to become a proxy" );
         _voters.modify( pitr, 0, [&]( auto& p ) {
          p.is_proxy = isproxy;
        });    
         update_votes(pitr->owner, pitr->proxy, pitr->producers, true);
      } else {
         _voters.emplace( proxy, [&]( auto& p ) {
            p.owner  = proxy;
            p.is_proxy = isproxy;
       });
      }
   }

   void system_contract::propagate_weight_change(const voter_info &voter) {
      eosio_assert( voter.proxy == 0 || !voter.is_proxy, "account registered as a proxy is not allowed to use a proxy");
      
      auto totalStake = voter.staked;
      if(voter.is_proxy){
         totalStake += voter.proxied_vote_weight;
      } 
      double new_weight = inverse_vote_weight((double)totalStake, voter.producers.size());
      double delta = new_weight - voter.last_vote_weight;

      if (voter.proxy) { // this part should never happen since the function is called only on proxies
         if(voter.last_stake != totalStake){
            auto &proxy = _voters.get(voter.proxy, "proxy not found"); // data corruption
            _voters.modify(proxy, 0, [&](auto &p) { 
               p.proxied_vote_weight += totalStake - voter.last_stake;
            });
            
            propagate_weight_change(proxy);
         }
      } else {
         for (auto acnt : voter.producers) {
            auto &pitr = _producers.get(acnt, "producer not found"); // data corruption
            _producers.modify(pitr, 0, [&](auto &p) {
               p.total_votes += delta;
               _gstate.total_producer_vote_weight += delta;
            });
         }
      }
      
      _voters.modify(voter, 0, [&](auto &v) { 
         v.last_vote_weight = new_weight; 
         v.last_stake = totalStake;
      });
   }

} /// namespace eosiosystem
