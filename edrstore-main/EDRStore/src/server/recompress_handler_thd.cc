/**
 * @file recompress_handler_thd.cc
 * @brief implement the recompress handler thread
 */

#include "../../include/server/recompress_handler_thd.h"

/**
 * @brief Construct a new RecompressHandlerThd object
 *
 * @param server_channel the SSL connection to the client
 */
RecompressHandlerThd::RecompressHandlerThd(SSLConnection* server_channel) {
    server_channel_ = server_channel;
    batch_size_ = config.GetSendChunkBatchSize();
}

/**
 * @brief Destroy the RecompressHandlerThd object
 */
RecompressHandlerThd::~RecompressHandlerThd() {
}

/**
 * @brief the main process
 *
 * @param cur_client current client var
 */
void RecompressHandlerThd::Run(ClientVar* cur_client) {
    tool::Logging(my_name_.c_str(), "the main thread is running.\n");
    AbsMQ<WrappedChunk_t>* input_MQ = cur_client->_recompress_mq;

    WrappedChunk_t* batch = new WrappedChunk_t[batch_size_];
    uint32_t batch_cnt = 0;
    uint32_t total_sent = 0;

    while (true) {
        if (input_MQ->_done && input_MQ->IsEmpty()) {
            break;
        }

        WrappedChunk_t chunk;
        if (input_MQ->Pop(chunk)) {
            // add to pending map for matching responses
            string fp_key((char*)chunk.info.compressed_fp, CHUNK_HASH_SIZE);
            {
                lock_guard<mutex> lck(cur_client->_recompress_map_lck);
                cur_client->_pending_recompress_map[fp_key] = chunk;
            }
            batch[batch_cnt++] = chunk;

            if (batch_cnt >= batch_size_) {
                this->SendRecompressBatch(cur_client, batch, batch_cnt);
                total_sent += batch_cnt;
                batch_cnt = 0;
            }
        }
    }

    // send remaining batch
    if (batch_cnt > 0) {
        this->SendRecompressBatch(cur_client, batch, batch_cnt);
        total_sent += batch_cnt;
    }

    delete[] batch;

    // always send completion signal so the client can exit its receive loop
    SendMsgBuffer_t complete_buf;
    complete_buf.send_buf = (uint8_t*) malloc(sizeof(NetworkHead_t));
    complete_buf.header = (NetworkHead_t*) complete_buf.send_buf;
    complete_buf.header->msg_type = CLIENT_RECOMPRESS_COMPLETE;
    complete_buf.header->client_id = cur_client->_client_id;
    complete_buf.header->cur_item_num = total_sent;
    complete_buf.header->size = 0;
    server_channel_->SendData(cur_client->_client_ssl, complete_buf.send_buf,
        sizeof(NetworkHead_t));
    free(complete_buf.send_buf);

    tool::Logging(my_name_.c_str(), "thread exits, total recompress requests: %u.\n",
        total_sent);
    return ;
}

/**
 * @brief send a batch of recompress requests to the client
 *
 * @param cur_client current client var
 * @param batch the batch of chunks
 * @param count the number of chunks in the batch
 */
void RecompressHandlerThd::SendRecompressBatch(ClientVar* cur_client,
    WrappedChunk_t* batch, uint32_t count) {
    size_t msg_size = sizeof(NetworkHead_t) + count * CHUNK_HASH_SIZE;
    uint8_t* send_buf = (uint8_t*) malloc(msg_size);
    NetworkHead_t* header = (NetworkHead_t*) send_buf;
    header->msg_type = SERVER_REQ_RECOMPRESS;
    header->client_id = cur_client->_client_id;
    header->cur_item_num = count;
    header->size = count * CHUNK_HASH_SIZE;

    uint8_t* fp_list = send_buf + sizeof(NetworkHead_t);
    for (uint32_t i = 0; i < count; i++) {
        memcpy(fp_list + i * CHUNK_HASH_SIZE,
            batch[i].info.compressed_fp, CHUNK_HASH_SIZE);
    }

    server_channel_->SendData(cur_client->_client_ssl, send_buf, msg_size);
    free(send_buf);
    return ;
}
