/**
 *-----------------------------------------------------------------------------
 * Title      : Memory Variable
 * ----------------------------------------------------------------------------
 * File       : Variable.cpp
 * ----------------------------------------------------------------------------
 * Description:
 * Interface between RemoteVariables and lower level memory transactions.
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
#include <rogue/interfaces/memory/Variable.h>
#include <rogue/interfaces/memory/Block.h>
#include <rogue/interfaces/memory/Constants.h>
#include <rogue/GilRelease.h>
#include <rogue/ScopedGil.h>
#include <rogue/GeneralError.h>
#include <boost/python.hpp>
#include <memory>
#include <cmath>

namespace rim = rogue::interfaces::memory;
namespace bp  = boost::python;


//! Class factory which returns a pointer to a Variable (VariablePtr)
rim::VariablePtr create ( std::string name,
                          std::string mode,
                          double   minimum,
                          double   maximum,
                          uint64_t offset,
                          std::vector<uint32_t> bitOffset,
                          std::vector<uint32_t> bitSize,
                          bool overlapEn,
                          bool verify,
                          bool bulkEn,
                          uint32_t modelId,
                          bool byteReverse,
                          uint32_t binPoint ) {

   rim::VariablePtr v = std::make_shared<rim::Variable>( name, mode, minimum, maximum,
         offset, bitOffset, bitSize, overlapEn, verify, bulkEn, modelId, byteReverse, binPoint);
   return(v);
}

// Setup class for use in python
void rim::Variable::setup_python() {

#ifndef NO_PYTHON
   bp::class_<rim::VariableWrap, rim::VariableWrapPtr, boost::noncopyable>("Variable",bp::init<std::string, std::string, bp::object, bp::object, uint64_t, bp::object, bp::object, bool,bool,bool,bp::object>())
      .def("_varBytes",        &rim::Variable::varBytes)
      .def("_offset",          &rim::Variable::offset)
      .def("_shiftOffsetDown", &rim::Variable::shiftOffsetDown)
      .def("_updatePath",      &rim::Variable::updatePath)
      .def("_bitOffset",       &rim::VariableWrap::bitOffset)
      .def("_bitSize",         &rim::VariableWrap::bitSize)
      .def("_get",             &rim::VariableWrap::get)
      .def("_set",             &rim::VariableWrap::set)
      .def("_queueUpdate",     &rim::Variable::queueUpdate, &rim::VariableWrap::defQueueUpdate)
   ;
#endif
}

// Create a Hub device with a given offset
rim::Variable::Variable ( std::string name,
                          std::string mode,
                          double   minimum,
                          double   maximum,
                          uint64_t offset,
                          std::vector<uint32_t> bitOffset,
                          std::vector<uint32_t> bitSize,
                          bool overlapEn,
                          bool verifyEn,
                          bool bulkEn,
                          uint32_t modelId,
                          bool byteReverse,
                          uint32_t binPoint) {

   uint32_t x;

   name_        = name;
   path_        = name;
   mode_        = mode;
   modelId_     = modelId;
   offset_      = offset;
   bitOffset_   = bitOffset;
   bitSize_     = bitSize;
   overlapEn_   = overlapEn;
   verifyEn_    = verifyEn;
   byteReverse_ = byteReverse;
   bulkEn_      = bulkEn;
   minValue_    = minimum;
   maxValue_    = maximum;
   binPoint_    = binPoint;
   stale_       = false;

   // Compute bit total
   bitTotal_ = bitSize_[0];
   for (x=1; x < bitSize_.size(); x++) bitTotal_ += bitSize_[x];

   // Compute rounded up byte size
   byteSize_  = (int)std::ceil((float)bitTotal_ / 8.0);

   // Compute total bit range of accessed bits
   varBytes_ = (int)std::ceil((float)(bitOffset_[bitOffset_.size()-1] + bitSize_[bitSize_.size()-1]) / 8.0);

   // Compute the lowest byte
   lowTranByte_  = (int)std::floor((float)bitOffset_[0] / 8.0);

   // Compute the highest byte
   highTranByte_ = varBytes_ - 1;

   // Custom data is NULL for now
   customData_ = NULL;

   // Define set function
   switch (modelId_) {
      case rim::PyFunc : setFuncPy_ = &rim::Block::setPyFunc;      break;
      case rim::Bytes  : setFuncPy_ = &rim::Block::setByteArrayPy; break;
      case rim::UInt : 
         if (bitTotal_ > 64) setFuncPy_ = &rim::Block::setPyFunc;
         else setFuncPy_ = &rim::Block::setUIntPy;
         break;
      case rim::Int :
         if (bitTotal_ > 64) setFuncPy_ = &rim::Block::setPyFunc;
         else setFuncPy_ = &rim::Block::setIntPy;
         break;
      case rim::Bool   : setFuncPy_ = &rim::Block::setBoolPy;   break;
      case rim::String : setFuncPy_ = &rim::Block::setStringPy; break;
      case rim::Float  : setFuncPy_ = &rim::Block::setFloatPy;  break;
      case rim::Fixed  : setFuncPy_ = &rim::Block::setFixedPy;  break;
      default          : getFuncPy_ = NULL; break;
   }

   // Define get function
   switch (modelId_) {
      case rim::PyFunc : getFuncPy_ = &rim::Block::getPyFunc;      break;
      case rim::Bytes  : getFuncPy_ = &rim::Block::getByteArrayPy; break;
      case rim::UInt : 
         if (bitTotal_ > 60) getFuncPy_ = &rim::Block::getPyFunc;
         else getFuncPy_ = &rim::Block::getUIntPy;
         break;
      case rim::Int :
         if (bitTotal_ > 60) getFuncPy_ = &rim::Block::getPyFunc;
         else getFuncPy_ = &rim::Block::getIntPy;
         break;
      case rim::Bool   : getFuncPy_ = &rim::Block::getBoolPy;   break;
      case rim::String : getFuncPy_ = &rim::Block::getStringPy; break;
      case rim::Float  : getFuncPy_ = &rim::Block::getFloatPy;  break;
      case rim::Fixed  : getFuncPy_ = &rim::Block::getFixedPy;  break;
      default          : getFuncPy_ = NULL; break;
   }

   // Set default C++ pointers
   setByteArray_ = &rim::Block::setByteArray;
   getByteArray_ = &rim::Block::getByteArray;

   setUInt_ = &rim::Block::setUInt;
   getUInt_ = &rim::Block::getUInt;

   setInt_ = &rim::Block::setInt;
   getInt_ = &rim::Block::getInt;

   setBool_ = &rim::Block::setBool;
   getBool_ = &rim::Block::getBool;

   setString_ = &rim::Block::setString;
   getString_ = &rim::Block::getString;

   setFloat_ = &rim::Block::setFloat;
   getFloat_ = &rim::Block::getFloat;

   setDouble_ = &rim::Block::setDouble;
   getDouble_ = &rim::Block::getDouble;

   setFixed_ = &rim::Block::setFixed;
   getFixed_ = &rim::Block::getFixed;
}

// Destroy the Hub
rim::Variable::~Variable() {}

// Shift offset down
void rim::Variable::shiftOffsetDown(uint32_t shift, uint32_t minSize) {
   uint32_t x;

   if (shift != 0) {
      offset_ -= shift;
      for (x=0; x < bitOffset_.size(); x++) bitOffset_[x] += shift*8;
   }

   // Compute total bit range of accessed bits, aligned to min size
   varBytes_ = (int)std::ceil((float)(bitOffset_[bitOffset_.size()-1] + bitSize_[bitSize_.size()-1]) / ((float)minSize*8.0)) * minSize;

   // Compute the lowest byte, aligned to min access
   lowTranByte_  = (int)std::floor((float)bitOffset_[0] / ((float)minSize*8.0)) * minSize;

   // Compute the highest byte, aligned to min access
   highTranByte_ = varBytes_ - 1;
}

void rim::Variable::updatePath(std::string path) {
   path_ = path;
}

// Return the name of the variable
std::string rim::Variable::name() {
   return name_;
}

// Return the mode of the variable
std::string rim::Variable::mode() {
   return mode_;
}

//! Return the minimum value
double rim::Variable::minimum() {
   return minValue_;
}

//! Return the maximum value
double rim::Variable::maximum() {
   return maxValue_;
}

//! Return variable range bytes
uint32_t rim::Variable::varBytes() {
   return varBytes_;
}

//! Get offset 
uint64_t rim::Variable::offset() {
   return offset_;
}

//! Return verify enable flag
bool rim::Variable::verifyEn() {
   return verifyEn_;
}

// Create a Variable
rim::VariableWrap::VariableWrap ( std::string name, 
                                  std::string mode, 
                                  bp::object minimum,
                                  bp::object maximum, 
                                  uint64_t offset,
                                  bp::object bitOffset,
                                  bp::object bitSize, 
                                  bool overlapEn, 
                                  bool verify,
                                  bool bulkEn,
                                  bp::object model)
                     : rim::Variable ( name, 
                                       mode, 
                                       py_object_convert<double>(minimum),
                                       py_object_convert<double>(maximum),
                                       offset,
                                       py_list_to_std_vector<uint32_t>(bitOffset),
                                       py_list_to_std_vector<uint32_t>(bitSize),
                                       overlapEn, 
                                       verify,
                                       bulkEn,
                                       bp::extract<uint32_t>(model.attr("modelId")),
                                       bp::extract<bool>(model.attr("isBigEndian")),
                                       bp::extract<uint32_t>(model.attr("binPoint")) ) {

   model_ = model;                     
}

//! Set value from RemoteVariable
void rim::VariableWrap::set(bp::object &value) {
   if (block_->blockTrans() ) return;
   (block_->*setFuncPy_)(value,this);
}

//! Get value from RemoteVariable
bp::object rim::VariableWrap::get() {
   return (block_->*getFuncPy_)(this);
}

// Set data using python function
bp::object rim::VariableWrap::toBytes ( bp::object &value ) {
   return model_.attr("toBytes")(value);
}

// Get data using python function
bp::object rim::VariableWrap::fromBytes ( bp::object &value ) {
   return model_.attr("fromBytes")(value);
}

void rim::Variable::queueUpdate() { }

void rim::VariableWrap::defQueueUpdate() {
   rim::Variable::queueUpdate();
}

// Queue update
void rim::VariableWrap::queueUpdate() {
   rogue::ScopedGil gil;

   if (bp::override pb = this->get_override("_queueUpdate")) {
      try {
         pb();
         return;
      } catch (...) {
         PyErr_Print();
      }
   }
}

bp::object rim::VariableWrap::bitOffset() {
   return std_vector_to_py_list<uint32_t>(bitOffset_);
}

bp::object rim::VariableWrap::bitSize() {
   return std_vector_to_py_list<uint32_t>(bitSize_);
}

/////////////////////////////////
// C++ Byte Array
/////////////////////////////////

void rim::Variable::setBytArray(uint8_t *data) {
   (block_->*setByteArray_)(data,this);
}

void rim::Variable::getByteArray(uint8_t *data) {
   (block_->*getByteArray_)(data,this);
}

/////////////////////////////////
// C++ Uint
/////////////////////////////////

void rim::Variable::setUInt(uint64_t &value) {
   (block_->*setUInt_)(value,this);
}

uint64_t rim::Variable::getUInt(rogue::interfaces::memory::Variable *) {
   return (block_->*getUInt_)(this);
}

/////////////////////////////////
// C++ int
/////////////////////////////////

void rim::Variable::setInt(int64_t &value, rogue::interfaces::memory::Variable *) {
   (block_->*setInt_)(value,this);
}

int64_t rim::Variable::getInt(rogue::interfaces::memory::Variable *) {
   return (block_->*getInt_)(this);
}

/////////////////////////////////
// C++ bool
/////////////////////////////////

void rim::Variable::setBool(bool &value, rogue::interfaces::memory::Variable *) {
   (block_->*setBool_)(value,this);
}

bool rim::Variable::getBool(rogue::interfaces::memory::Variable *) {
   return (block_->*getBool_)(this);
}

/////////////////////////////////
// C++ String
/////////////////////////////////

void rim::Variable::setString(std::string &value, rogue::interfaces::memory::Variable *) {
   (block_->*setString_)(value,this);
}

std::string rim::Variable::getString(rogue::interfaces::memory::Variable *) {
   return (block_->*getString_)(this);
}

/////////////////////////////////
// C++ Float
/////////////////////////////////

void rim::Variable::setFloat(float &value, rogue::interfaces::memory::Variable *) {
   (block_->*setFloat_)(value,this);
}

float rim::Variable::getFloat(rogue::interfaces::memory::Variable *) {
   return (block_->*getFloat_)(this);
}

/////////////////////////////////
// C++ double
/////////////////////////////////

void rim::Variable::setDouble(double &value, rogue::interfaces::memory::Variable *) {
   (block_->*setDouble_)(value,this);
}

double rim::Variable::getDouble(rogue::interfaces::memory::Variable *) {
   return (block_->*getDouble_)(this);
}

/////////////////////////////////
// C++ filed point
/////////////////////////////////

void rim::Variable::setFixed(double &, rogue::interfaces::memory::Variable *) {
   (block_->*setFixed_)(value,this);
}

double rim::Variable::getFixed(rogue::interfaces::memory::Variable *) {
   return (block_->*getFixed_)(this);
}
