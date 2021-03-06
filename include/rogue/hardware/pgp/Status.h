/**
 *-----------------------------------------------------------------------------
 * Title      : PGP Card Status Class
 * ----------------------------------------------------------------------------
 * Description:
 * Wrapper for PgpStatus structure
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
#ifndef __ROGUE_HARDWARE_PGP_STATUS_H__
#define __ROGUE_HARDWARE_PGP_STATUS_H__
#include <rogue/hardware/drivers/PgpDriver.h>
#include <stdint.h>
#include <memory>

namespace rogue {
   namespace hardware {
      namespace pgp {

         //! PGP Status Class
         /** This class contains the current PGP status for one of the 8 lanes
          * on a PGP card. This class is a C++ wrapper around the PgpStatus
          * structure used by the lower level driver. All structure members are
          * exposed to Python using their original names and can be read directly.
          */
         class Status : public PgpStatus {
            public:

               // Create the info class with pointer
               static std::shared_ptr<rogue::hardware::pgp::Status> create();

               // Setup class in python
               static void setup_python();

         };

         //! Alias for using shared pointer as StatusPtr
         typedef std::shared_ptr<rogue::hardware::pgp::Status> StatusPtr;
      }
   }
}

#endif

