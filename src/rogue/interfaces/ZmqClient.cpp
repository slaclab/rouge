/**
 *-----------------------------------------------------------------------------
 * Title      : Rogue ZMQ Control Interface
 * ----------------------------------------------------------------------------
 * File       : ZmqClient.cpp
 * Created    : 2019-05-02
 * ----------------------------------------------------------------------------
 * This file is part of the rogue software platform. It is subject to 
 * the license terms in the LICENSE.txt file found in the top-level directory 
 * of this distribution and at: 
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
 * No part of the rogue software platform, including this file, may be 
 * copied, modified, propagated, or distributed except according to the terms 
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
**/
#include <rogue/interfaces/ZmqClient.h>
#include <rogue/GeneralError.h>
#include <memory>
#include <rogue/GilRelease.h>
#include <rogue/ScopedGil.h>
#include <string>
#include <zmq.h>

#ifndef NO_PYTHON
#include <boost/python.hpp>
namespace bp = boost::python;
#endif

rogue::interfaces::ZmqClientPtr rogue::interfaces::ZmqClient::create(std::string addr, uint16_t port) {
   rogue::interfaces::ZmqClientPtr ret = std::make_shared<rogue::interfaces::ZmqClient>(addr,port);
   return(ret);
}

//! Setup class in python
void rogue::interfaces::ZmqClient::setup_python() {
#ifndef NO_PYTHON

   bp::class_<rogue::interfaces::ZmqClientWrap, rogue::interfaces::ZmqClientWrapPtr, boost::noncopyable>("ZmqClient",bp::init<std::string, uint16_t>())
      .def("_doUpdate", &rogue::interfaces::ZmqClient::doUpdate, &rogue::interfaces::ZmqClientWrap::defDoUpdate)
      .def("_send",     &rogue::interfaces::ZmqClient::send)
   ;
#endif
}

rogue::interfaces::ZmqClient::ZmqClient (std::string addr, uint16_t port) {
   std::string temp;
   int32_t opt;

   this->zmqCtx_  = zmq_ctx_new();
   this->zmqSub_  = zmq_socket(this->zmqCtx_,ZMQ_SUB);
   this->zmqReq_  = zmq_socket(this->zmqCtx_,ZMQ_REQ);

   // Setup sub port
   temp = "tcp://";
   temp.append(addr);
   temp.append(":");
   temp.append(std::to_string(static_cast<long long>(port)));

   //opt = 100;
   if ( zmq_setsockopt (this->zmqSub_, ZMQ_RCVTIMEO, &opt, sizeof(int32_t)) != 0 ) 
         throw(rogue::GeneralError("ZmqClient::ZmqClient","Failed to set socket timeout"));

   if ( zmq_setsockopt (this->zmqSub_, ZMQ_SUBSCRIBE, "", 0) != 0 )
         throw(rogue::GeneralError("ZmqClient::ZmqClient","Failed to set socket subscribe"));

   if ( zmq_connect(this->zmqSub_,temp.c_str()) < 0 ) 
      throw(rogue::GeneralError::network("ZmqClient::ZmqClient",addr,port));

   // Setup request port
   temp = "tcp://";
   temp.append(addr);
   temp.append(":");
   temp.append(std::to_string(static_cast<long long>(port+1)));

   //opt = 100;
   //if ( zmq_setsockopt (this->zmqReq_, ZMQ_RCVTIMEO, &opt, sizeof(int32_t)) != 0 ) 
         //throw(rogue::GeneralError("ZmqClient::ZmqClient","Failed to set socket timeout"));

   if ( zmq_connect(this->zmqReq_,temp.c_str()) < 0 ) 
      throw(rogue::GeneralError::network("ZmqClient::ZmqClient",addr,port+1));

   threadEn_ = true;
   thread_ = new std::thread(&rogue::interfaces::ZmqClient::runThread, this);
}

rogue::interfaces::ZmqClient::~ZmqClient() {
   rogue::GilRelease noGil;
   threadEn_ = false;
   thread_->join();

   zmq_close(this->zmqSub_);
   zmq_close(this->zmqReq_);
   zmq_term(this->zmqCtx_);
}

std::string rogue::interfaces::ZmqClient::send(std::string value) {
   zmq_msg_t msg;
   std::string data;

   rogue::GilRelease noGil;
   zmq_send(this->zmqReq_,value.c_str(),value.size(),0);

   zmq_msg_init(&msg);
   zmq_recvmsg(this->zmqReq_,&msg,0);
   data = std::string((const char *)zmq_msg_data(&msg),zmq_msg_size(&msg));
   zmq_msg_close(&msg);

   return data;
}

void rogue::interfaces::ZmqClient::doUpdate ( std::string data ) { }

#ifndef NO_PYTHON

rogue::interfaces::ZmqClientWrap::ZmqClientWrap (std::string addr, uint16_t port) : rogue::interfaces::ZmqClient(addr,port) {}

void rogue::interfaces::ZmqClientWrap::doUpdate ( std::string data ) {
   {
      rogue::ScopedGil gil;

      if (bp::override f = this->get_override("_doUpdate")) {
         try {
            f(data);
         } catch (...) {
            PyErr_Print();
         }
      }
   }
   rogue::interfaces::ZmqClient::doUpdate(data);
}

void rogue::interfaces::ZmqClientWrap::defDoUpdate ( std::string data ) { 
   rogue::interfaces::ZmqClient::doUpdate(data);
}

#endif

void rogue::interfaces::ZmqClient::runThread() {
   std::string data;
   std::string ret;
   zmq_msg_t msg;

   while(threadEn_) {
      zmq_msg_init(&msg);

      // Get the message
      if ( zmq_recvmsg(this->zmqSub_,&msg,0) > 0 ) {
         data = std::string((const char *)zmq_msg_data(&msg),zmq_msg_size(&msg));
         printf("received message %s\n",data.c_str());
         zmq_msg_close(&msg);
         this->doUpdate(data);
      }
   }
}


