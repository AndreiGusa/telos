/**
 * 
 * 
 */

#include <trail.definitions/traildef.voting.hpp>
#include <trail.definitions/traildef.system.hpp>
#include <trail.definitions/traildef.tokens.hpp>

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/singleton.hpp>
#include <string>

using namespace eosio;

class trail : public contract {
    
    public:

        trail(account_name self);

        ~trail();

        #pragma region Actions

        /// @abi action
        void regtoken(asset native, account_name publisher);

        /// @abi action
        void unregtoken(asset native, account_name publisher);

        /// @abi action
        void regvoter(account_name voter);

        /// @abi action
        void unregvoter(account_name voter);

        /// @abi action
        void regballot(account_name publisher, asset voting_token);

        /// @abi action
        void unregballot(account_name publisher);

        #pragma endregion Actions

        #pragma region Reactions

        //called by delegatebw/undelegatebw
        //void updatevr(uint64_t vote_code, uint64_t vote_scope, uint64_t vote_key, symbol_name vote_token, uint16_t direction, uint32_t expiration, account_name voter);

        //called by voting contract running processvote
        //void processvr(uint64_t vote_code, uint64_t vote_scope, uint64_t vote_key, symbol_name vote_token, uint16_t direction, account_name voter);

        #pragma endregion Reactions

    protected:

        environment_singleton env_singleton;
        environment env_struct;
};