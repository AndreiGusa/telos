#include <eosiolib/eosio.hpp>

#include "../trail.service/trail.defs/trail.voting.hpp"
#include "../trail.service/trail.defs/trail.system.hpp"

class workerproposal : public contract {
    public:

      workerproposal(account_name self);

      ~workerproposal();

      /// @abi action
      void submit(account_name proposer, std::string title, uint16_t cycles, std::string ipfs_location, asset amount, account_name receiver);

      /// @abi action
      void linkballot(uint64_t prop_id, uint64_t ballot_id, account_name proposer);

      /// @abi action
      void claim(uint64_t prop_id, account_name proposer);

      //@abi table proposals i64
      struct proposal {
        uint64_t         id;
        uint64_t         ballot_id;
        account_name     proposer;
        account_name     receiver;
        std::string      title;
        std::string      ipfs_location;
        uint16_t         cycles;
        uint64_t         amount;
        uint64_t         fee;
        uint32_t         begin_time;
        uint32_t         end_time;
        uint8_t          status; // 0 = INACTIVE, 1 = ACTIVE
        uint16_t         current_cycle;

        auto primary_key() const { return id; }
      };

      struct wp_env {
        account_name publisher;
        uint32_t cycle_duration;
        uint16_t fee_percentage;
        uint64_t fee_min;

        uint64_t primary_key() const { return publisher; }
      };
      wp_env wp_env_struct;

      typedef eosio::multi_index< N(proposals), proposal> proposals;
      typedef singleton<N(wp_env), wp_env> wp_environment_singleton;

      wp_environment_singleton wp_env_singleton;    
};
