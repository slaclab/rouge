/**
 *-----------------------------------------------------------------------------
 * Title      : PGP Card Class
 * ----------------------------------------------------------------------------
 * File       : PgpCard.h
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2017-09-17
 * Last update: 2017-09-17
 * ----------------------------------------------------------------------------
 * Description:
 * PGP Card Class
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
#include <rogue/hardware/pgp/PgpCard.h>
#include <rogue/hardware/pgp/Info.h>
#include <rogue/hardware/pgp/Status.h>
#include <rogue/hardware/pgp/PciStatus.h>
#include <rogue/hardware/pgp/EvrStatus.h>
#include <rogue/hardware/pgp/EvrControl.h>
#include <rogue/interfaces/stream/Frame.h>
#include <rogue/interfaces/stream/Buffer.h>
#include <rogue/exceptions/OpenException.h>
#include <rogue/exceptions/GeneralException.h>
#include <rogue/exceptions/TimeoutException.h>
#include <boost/make_shared.hpp>

namespace rhp = rogue::hardware::pgp;
namespace ris = rogue::interfaces::stream;
namespace re  = rogue::exceptions;
namespace bp  = boost::python;

//! Class creation
rhp::PgpCardPtr rhp::PgpCard::create (std::string path, uint32_t lane, uint32_t vc) {
   rhp::PgpCardPtr r = boost::make_shared<rhp::PgpCard>(path,lane,vc);
   return(r);
}

//! Creator
rhp::PgpCard::PgpCard ( std::string path, uint32_t lane, uint32_t vc ) {
   uint32_t mask;

   lane_    = lane;
   vc_      = vc;
   timeout_ = 1000000;

   mask = (1 << ((lane_*4) +vc_));

   if ( (fd_ = ::open(path.c_str(), O_RDWR)) < 0 ) 
      throw(re::OpenException(path.c_str(),mask));

   if ( pgpSetMask(fd_,mask) < 0 ) {
      ::close(fd_);
      throw(re::OpenException(path.c_str(),mask));
   }

   // Result may be that rawBuff_ = NULL
   rawBuff_ = pgpMapDma(fd_,&bCount_,&bSize_);

   // Start read thread
   thread_ = new boost::thread(boost::bind(&rhp::PgpCard::runThread, this));
}

//! Destructor
rhp::PgpCard::~PgpCard() {
   thread_->interrupt();
   thread_->join();

   if ( rawBuff_ != NULL ) pgpUnMapDma(fd_, rawBuff_);
   ::close(fd_);
}

//! Set timeout for frame transmits in microseconds
void rhp::PgpCard::setTimeout(uint32_t timeout) {
   if ( timeout == 0 ) timeout_ = 1;
   else timeout_ = timeout;
}

//! Get card info.
rhp::InfoPtr rhp::PgpCard::getInfo() {
   rhp::InfoPtr r = rhp::Info::create();
   pgpGetInfo(fd_,r.get());
   return(r);
}

//! Get pci status.
rhp::PciStatusPtr rhp::PgpCard::getPciStatus() {
   rhp::PciStatusPtr r = rhp::PciStatus::create();
   pgpGetPci(fd_,r.get());
   return(r);
}

//! Get status of open lane.
rhp::StatusPtr rhp::PgpCard::getStatus() {
   rhp::StatusPtr r = rhp::Status::create();
   pgpGetStatus(fd_,lane_,r.get());
   return(r);
}

//! Get evr control for open lane.
rhp::EvrControlPtr rhp::PgpCard::getEvrControl() {
   rhp::EvrControlPtr r = rhp::EvrControl::create();
   pgpGetEvrControl(fd_,lane_,r.get());
   return(r);
}

//! Set evr control for open lane.
void rhp::PgpCard::setEvrControl(rhp::EvrControlPtr r) {
   pgpSetEvrControl(fd_,lane_,r.get());
}

//! Get evr status for open lane.
rhp::EvrStatusPtr rhp::PgpCard::getEvrStatus() {
   rhp::EvrStatusPtr r = rhp::EvrStatus::create();
   pgpGetEvrStatus(fd_,lane_,r.get());
   return(r);
}

//! Set loopback for open lane
void rhp::PgpCard::setLoop(bool enable) {
   pgpSetLoop(fd_,lane_,enable);
}

//! Set lane data for open lane
void rhp::PgpCard::setData(uint8_t data) {
   pgpSetData(fd_,lane_,data);
}

//! Send an opcode
void rhp::PgpCard::sendOpCode(uint8_t code) {
   pgpSendOpCode(fd_,code);
}

//! Generate a buffer. Called from master
ris::FramePtr rhp::PgpCard::acceptReq ( uint32_t size, bool zeroCopyEn ) {
   int32_t          res;
   fd_set           fds;
   struct timeval   tout;
   uint32_t         alloc;
   ris::BufferPtr   buff;
   ris::FramePtr    frame;

   // Zero copy is disabled. Allocate from memory.
   if ( zeroCopyEn == false || rawBuff_ == NULL ) {
      frame = createFrame(size,bSize_,true,false);
   }

   // Allocate zero copy buffers from driver
   else {

      boost::lock_guard<boost::mutex> lock(mtx_);

      // Create empty frame
      frame = createFrame(0,0,true,true);
      alloc=0;

      // Request may be serviced with multiple buffers
      while ( alloc < size ) {

         // Keep trying since select call can fire 
         // but getIndex fails because we did not win the buffer lock
         do {

            // Setup fds for select call
            FD_ZERO(&fds);
            FD_SET(fd_,&fds);

            // Setup select timeout
            tout.tv_sec=timeout_ / 1000000;
            tout.tv_usec=timeout_ % 1000000;

            if ( (res = select(fd_+1,NULL,&fds,NULL,&tout)) == 0 ) 
               throw(re::TimeoutException(timeout_));
            
            // Attempt to get index.
            // return of less than 0 is a failure to get a buffer
            res = pgpGetIndex(fd_);
         }
         while (res < 0);

         buff = createBuffer(rawBuff_[res],0x80000000 | res,bSize_);
         frame->appendBuffer(buff);
         alloc += bSize_;
      }
   }
   return(frame);
}

//! Accept a frame from master
void rhp::PgpCard::acceptFrame ( ris::FramePtr frame ) {
   ris::BufferPtr buff;
   int32_t          res;
   fd_set           fds;
   struct timeval   tout;
   uint32_t         meta;
   uint32_t         x;
   uint32_t         cont;

   boost::lock_guard<boost::mutex> lock(mtx_);

   // Go through each buffer in the frame
   for (x=0; x < frame->getCount(); x++) {
      buff = frame->getBuffer(x);

      // Continue flag is set if this is not the last buffer
      if ( x == (frame->getCount() - 1) ) cont = 0;
      else cont = 1;

      // Get buffer meta field
      meta = buff->getMeta();

      // Meta is zero copy as indicated by bit 31
      if ( (meta & 0x80000000) != 0 ) {

         // Buffer is not already stale as indicates by bit 30
         if ( (meta & 0x40000000) == 0 ) {

            // Write by passing buffer index to driver
            if ( pgpWriteIndex(fd_, meta & 0x3FFFFFFF, buff->getCount(), lane_, vc_, cont) <= 0 ) 
               throw(re::GeneralException("PGP Write Call Failed"));

            // Mark buffer as stale
            meta |= 0x40000000;
            buff->setMeta(meta);
         }
      }

      // Write to pgp with buffer copy in driver
      else {

         // Keep trying since select call can fire 
         // but write fails because we did not win the buffer lock
         do {

            // Setup fds for select call
            FD_ZERO(&fds);
            FD_SET(fd_,&fds);

            // Setup select timeout
            tout.tv_sec=timeout_ / 1000000;
            tout.tv_usec=timeout_ % 1000000;

            if ( (res = select(fd_+1,NULL,&fds,NULL,&tout)) == 0 ) 
               throw(re::TimeoutException(timeout_));

            // Write with buffer copy
            res = pgpWrite(fd_, buff->getRawData(), buff->getCount(), lane_, vc_, cont);

            // Error
            if ( res < 0 ) throw(re::GeneralException("PGP Write Call Failed"));
         }

         // Exit out if return flag was set false
         while ( res == 0 );
      }
   }
}

//! Return a buffer
void rhp::PgpCard::retBuffer(uint8_t * data, uint32_t meta, uint32_t rawSize) {

   // Buffer is zero copy as indicated by bit 31
   if ( (meta & 0x80000000) != 0 ) {
      boost::lock_guard<boost::mutex> lock(mtx_);

      // Device is open and buffer is not stale
      // Bit 30 indicates buffer has already been returned to hardware
      if ( (fd_ >= 0) && ((meta & 0x40000000) == 0) ) {
         pgpRetIndex(fd_,meta & 0x3FFFFFFF); // Return to hardware
      }
      deleteBuffer(rawSize);
   }

   // Buffer is allocated from Slave class
   else Slave::retBuffer(data,meta,rawSize);
}

//! Run thread
void rhp::PgpCard::runThread() {
   ris::BufferPtr buff;
   ris::FramePtr  frame;
   fd_set         fds;
   int32_t        res;
   uint32_t       error;
   uint32_t       cont;
   uint32_t       meta;
   struct timeval tout;

   // Preallocate empty frame
   frame = createFrame(0,0,false,(rawBuff_ != NULL));

   try {
      while(1) {

         // Setup fds for select call
         FD_ZERO(&fds);
         FD_SET(fd_,&fds);

         // Setup select timeout
         tout.tv_sec  = 0;
         tout.tv_usec = 100;

         // Select returns with available buffer
         if ( select(fd_+1,&fds,NULL,NULL,&tout) > 0 ) {

            // Zero copy buffers were not allocated
            if ( rawBuff_ == NULL ) {

               // Allocate a buffer
               buff = allocBuffer(bSize_);

               // Attempt read, lane and vc not needed since only one lane/vc is open
               res = pgpRead(fd_, buff->getRawData(), buff->getRawSize(), NULL, NULL, &error, &cont);
            }

            // Zero copy read
            else {

               // Attempt read, lane and vc not needed since only one lane/vc is open
               if ((res = pgpReadIndex(fd_, &meta, NULL, NULL, &error, &cont)) > 0) {

                  // Allocate a buffer, Mark zero copy meta with bit 31 set, lower bits are index
                  buff = createBuffer(rawBuff_[meta],0x80000000 | meta,bSize_);
               }
            }

            // Read was successfull
            if ( res > 0 ) {
               buff->setSize(res);
               buff->setError(error);
               frame->setError(error | frame->getError());
               frame->appendBuffer(buff);

               // If continue flag is not set, push frame and get a new empty frame
               if ( cont == 0 ) {
                  sendFrame(frame);
                  frame = createFrame(0,0,false,(rawBuff_ != NULL));
               }
            }
         }
      }
   } catch (boost::thread_interrupted&) { }
}

void rhp::PgpCard::setup_python () {

   bp::class_<rhp::PgpCard, rhp::PgpCardPtr, bp::bases<ris::Master,ris::Slave>, boost::noncopyable >("PgpCard",bp::init<std::string,uint32_t,uint32_t>())
      .def("create",         &rhp::PgpCard::create)
      .staticmethod("create")
      .def("getInfo",        &rhp::PgpCard::getInfo)
      .def("getPciStatus",   &rhp::PgpCard::getPciStatus)
      .def("getStatus",      &rhp::PgpCard::getStatus)
      .def("getEvrControl",  &rhp::PgpCard::getEvrControl)
      .def("setEvrControl",  &rhp::PgpCard::setEvrControl)
      .def("getEvrStatus",   &rhp::PgpCard::getEvrStatus)
      .def("setLoop",        &rhp::PgpCard::setLoop)
      .def("setData",        &rhp::PgpCard::setData)
      .def("sendOpCode",     &rhp::PgpCard::sendOpCode)
   ;

   bp::implicitly_convertible<rhp::PgpCardPtr, ris::MasterPtr>();
   bp::implicitly_convertible<rhp::PgpCardPtr, ris::SlavePtr>();

}

