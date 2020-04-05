#include <chainparams.h>
#include <stdexcept>

namespace btc_utils
{

network_t g_network = network_t::testnet;

/** The maximum allowed size for a serialized block, in bytes (only for buffer size limits) */
const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 4000000;

/**
 * The message start string is designed to be unlikely to occur in normal data.
 * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
 * a large 32-bit integer with any alignment.
 */
start_marker_t mainnet_marker = {0xf9,0xbe,0xb4,0xd9};
start_marker_t testnet_marker = {0x0b,0x11,0x09,0x07};
start_marker_t regtest_marker = {0xfa,0xbf,0xb5,0xda};

const start_marker_t& message_start()
{
   switch(g_network)
   {
   case(network_t::mainnet):
      return mainnet_marker;
   case(network_t::testnet):
      return testnet_marker;
   case(network_t::regtest):
      return regtest_marker;
   }
   throw std::runtime_error("Unknown network type");
}

}