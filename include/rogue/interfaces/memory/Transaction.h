/**
 *-----------------------------------------------------------------------------
 * Title      : Memory Transaction
 * ----------------------------------------------------------------------------
 * File       : Transaction.h
 * Created    : 2019-03-08
 * ----------------------------------------------------------------------------
 * Description:
 * Memory Transaction
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
#ifndef __ROGUE_INTERFACES_MEMORY_MASTER_H__
#define __ROGUE_INTERFACES_MEMORY_MASTER_H__
#include <stdint.h>
#include <vector>
#include <boost/python.hpp>
#include <boost/thread.hpp>

namespace rogue {
   namespace interfaces {
      namespace memory {

         class Master;

         //! Transaction
         class Transaction {
            friend rogue::interfaces::memory::Master;
   
            private:

               //! Class instance counter
               static uint32_t classIdx_;

               //! Class instance lock
               static boost::mutex classMtx_;

            protected:

               //! Create a transaction container
               static boost::shared_ptr<rogue::interfaces::memory::Transaction> create (
                  boost::shared_ptr<rogue::interfaces::memory::Master> master);

               //! Setup class in python
               static void setup_python();

               //! Constructor
               Transaction(boost::shared_ptr<rogue::interfaces::memory::Master> master);

               //! Associated master
               rogue::interfaces::memory::Master master_;

               //! Transaction start time
               struct timeval endTime_;

               //! Transaction start time
               struct timeval startTime_;

               //! Transaction python buffer
               Py_buffer pyBuf_;

               //! Python buffer is valid
               bool pyValid_;

               //! Transaction data
               uint8_t * data_;

               //! Transaction address
               uint64_t address_;

               //! Transaction size
               uint32_t size_;

               //! Transaction type
               uint32_t type_;

               //! Transaction error
               uint32_t error_;

               //! Transaction id
               uint32_t id_;

            public:

               //! Transaction lock
               boost::mutex lock;

               //! Destructor
               ~Transaction();

               //! Get id
               uint32_t id();

               //! Get address
               uint32_t address();

               //! Get size
               uint32_t size();

               //! Get type
               uint32_t type();

               //! Complete transaction with passed error
               void complete(uint32_t error);

               //! start iterator, caller must lock around access
               rogue::interfaces::memory::Transaction::iterator begin();

               //! end iterator, caller must lock around access
               rogue::interfaces::memory::Transaction::iterator end();

               //! Write data from python
               void writePy ( boost::python::object p, uint32_t offset );

               //! Read data from python
               void readPy ( boost::python::object p, uint32_t offset );
         };

         // Convienence
         typedef boost::shared_ptr<rogue::interfaces::memory::Transaction> TransactionPtr;

      }
   }
}

#endif

