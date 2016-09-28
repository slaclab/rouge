/**
 *-----------------------------------------------------------------------------
 * Title      : Memory Master
 * ----------------------------------------------------------------------------
 * File       : Master.cpp
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-09-20
 * Last update: 2016-09-20
 * ----------------------------------------------------------------------------
 * Description:
 * Memory master interface.
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
#include <rogue/interfaces/memory/Master.h>
#include <rogue/interfaces/memory/Slave.h>
#include <boost/make_shared.hpp>

namespace rim = rogue::interfaces::memory;
namespace bp  = boost::python;

// Init class counter
uint32_t rim::Master::classIdx_ = 0;

//! Class instance lock
boost::mutex rim::Master::classIdxMtx_;

//! Create a master container
rim::MasterPtr rim::Master::create () {
   rim::MasterPtr m = boost::make_shared<rim::Master>();
   return(m);
}

//! Create object
rim::Master::Master() { 
   classIdxMtx_.lock();
   if ( classIdx_ == 0 ) classIdx_ = 1;
   index_ = classIdx_;
   classIdx_++;
   classIdxMtx_.unlock();
   slave_ = rim::Slave::create();
} 

//! Destroy object
rim::Master::~Master() { }

//! Get index
uint32_t rim::Master::getIndex() {
   return(index_);
}

//! Set slave, used for buffer request forwarding
void rim::Master::setSlave ( rim::SlavePtr slave ) {
   boost::lock_guard<boost::mutex> lock(slaveMtx_);
   slave_ = slave;
}

//! Get slave
rim::SlavePtr rim::Master::getSlave () {
   boost::lock_guard<boost::mutex> lock(slaveMtx_);
   return(slave_);
}

//! Post a transaction, called locally, forwarded to slave
void rim::Master::reqTransaction(uint64_t address, uint32_t size, bool write, bool posted) {
   boost::lock_guard<boost::mutex> lock(slaveMtx_);
   slave_->doTransaction(shared_from_this(),address,size,write,posted);
}

//! Transaction complete, set by slave, error passed
void rim::Master::doneTransaction(uint32_t error) { }

//! Set to master from slave, called by slave
void rim::Master::setData(void *data, uint32_t offset, uint32_t size) { }

//! Get from master to slave, called by slave
void rim::Master::getData(void *data, uint32_t offset, uint32_t size) { }

void rim::Master::setup_python() {

   // slave can only exist as sub-class in python
   bp::class_<rim::Master, rim::MasterPtr, boost::noncopyable>("Master",bp::init<>())
      .def("setSlave",       &rim::Master::setSlave)
   ;

}

