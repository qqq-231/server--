/**
 * @file recompress_handler_thd.h
 * @brief handle recompress requests to client for similarity-rejected chunks
 */

#ifndef EDRSTORE_RECOMPRESS_HANDLER_THD_H
#define EDRSTORE_RECOMPRESS_HANDLER_THD_H

#include "client_var.h"
#include "../network/ssl_conn.h"
#include "../configure.h"

extern Configure config;

class RecompressHandlerThd {
    private:
        string my_name_ = "RecompressHandlerThd";
        SSLConnection* server_channel_;
        uint64_t batch_size_;

        /**
         * @brief send a batch of recompress requests to the client
         *
         * @param cur_client current client var
         * @param batch the batch of chunks
         * @param count the number of chunks in the batch
         */
        void SendRecompressBatch(ClientVar* cur_client,
            WrappedChunk_t* batch, uint32_t count);

    public:
        /**
         * @brief Construct a new RecompressHandlerThd object
         *
         * @param server_channel the SSL connection to the client
         */
        RecompressHandlerThd(SSLConnection* server_channel);

        /**
         * @brief Destroy the RecompressHandlerThd object
         */
        ~RecompressHandlerThd();

        /**
         * @brief the main process
         *
         * @param cur_client current client var
         */
        void Run(ClientVar* cur_client);
};

#endif
