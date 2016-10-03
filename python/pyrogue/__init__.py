#!/usr/bin/env python
#-----------------------------------------------------------------------------
# Title      : PyRogue base module
#-----------------------------------------------------------------------------
# File       : pyrogue/__init__.py
# Author     : Ryan Herbst, rherbst@slac.stanford.edu
# Created    : 2016-09-29
# Last update: 2016-09-29
#-----------------------------------------------------------------------------
# Description:
# Module containing the top functions and classes within the pyrouge library
#-----------------------------------------------------------------------------
# This file is part of the rogue software platform. It is subject to 
# the license terms in the LICENSE.txt file found in the top-level directory 
# of this distribution and at: 
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
# No part of the rogue software platform, including this file, may be 
# copied, modified, propagated, or distributed except according to the terms 
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------
import rogue.interfaces.memory
import textwrap
import yaml

def streamConnect(source, dest):
    """Connect soruce and destination stream devices"""

    # Is object a native master or wrapped?
    if isinstance(source,rogue.interfaces.stream.Master):
        master = source
    else:
        master = source._getStreamMaster()

    # Is object a native slave or wrapped?
    if isinstance(dest,rogue.interfaces.stream.Slave):
        slave = dest
    else:
        slave = dest._getStreamSlave()

    master._setSlave(slave)


def streamTap(source, tap):
    """Add a stream tap"""

    # Is object a native master or wrapped?
    if isinstance(source,rogue.interfaces.stream.Master):
        master = source
    else:
        master = source._getStreamMaster()

    # Is object a native slave or wrapped?
    if isinstance(tap,rogue.interfaces.stream.Slave):
        slave = tap
    else:
        slave = tap._getStreamSlave()

    master._addSlave(slave)


def streamConnectBiDir(deviceA, deviceB):
    """Connect two endpoints of a bi-directional stream"""

    streamConnect(deviceA,deviceB)
    streamConnect(deviceB,deviceA)


def busConnect(source,dest):
    """Connector a memory bus client and server"""

    # Is object a native master or wrapped?
    if isinstance(source,rogue.interfaces.stream.Master):
        master = source
    else:
        master = source._getMemoryMaster()

    # Is object a native slave or wrapped?
    if isinstance(dest,rogue.interfaces.stream.Slave):
        slave = dest
    else:
        slave = dest._getMemorySlave()

    master._setSlave(slave)


class VariableError(Exception):
    pass


class NodeError(Exception):
    pass


class Root(rogue.interfaces.stream.Master):
    """System base"""

    def __init__(self,name):
        rogue.interfaces.stream.Master.__init__(self)

        self.name = name

        # Config write command exposed to higher level
        Command(parent=self, name='writeConfig',description='Write Configuration',
           function=self._writeYamlConfig)

        # Config read command exposed to higher level
        Command(parent=self, name='readConfig',description='Read Configuration',
           function=self._readYamlConfig)

    # Push configuration on stream
    def streamConfig(self):
        """Push confiuration string out on a stream"""
        s = self.getYamlConfig()
        f = self._reqFrame(len(s),True,0)
        b = bytearray()
        b.extend(s)
        f.write(b,0)
        self._sendFrame(f)

    def getStructure(self):
        """Get structure as a dictionary"""
        return {self.name:_getStructure(self)}

    def getYamlStructure(self):
        """Get structure as a yaml string"""
        return yaml.dump(self.getStructure(),default_flow_style=False)

    def getConfig(self):
        """Get configuration as a dictionary"""
        self.readAll()
        return {self.name:_getConfig(self)}

    def getYamlConfig(self):
        """Get configuration as a yaml string"""
        return yaml.dump(self.getConfig(),default_flow_style=False)

    def setConfig(self,d):
        """Set configuration from a dictionary"""
        for key, value in d.iteritems():
            if key == self.name:
                _setConfig(self,value)
        self.writeAll()

    def setYamlConfig(self,yml):
        """Set configuration from a yaml string"""
        d = yaml.load(yml)
        self.setConfig(d)

    def writeAll(self):
        """Write all blocks"""
        _writeAll(self)

    def writeStale(self):
        """Write stale blocks"""
        _writeStale(self)

    def readAll(self):
        """Read all blocks"""
        _readAll(self)

    def readPollable(self):
        """Read pollable blocks"""
        _readPollable(self)

    def checkUpdatedBlocks(self):
        """Check status of updated blocks"""
        _checkUpdatedBlocks(self)

    def getAtPath(self,path):
        """Get dictionary entry at path"""
        return(_getAtPath(self,path))

    def _writeYamlConfig(self,cmd,arg):
        """Write YAML configuration to a file. Called from command"""
        with open(arg,'w') as f:
            f.write(self.getYamlConfig())

    def _readYamlConfig(self,cmd,arg):
        """Read YAML configuration from a file. Called from command"""
        with open(arg,'r') as f:
            self.setYamlConfig(f.read())


class Node(object):
    """Common system node"""

    def __init__(self, parent, name, description, hidden, classType):

        # Public attributes
        self.name        = name     
        self.description = description
        self.hidden      = hidden
        self.classType   = classType

        # Tracking
        self._parent = parent

        if isinstance(parent,Node):
            self._root = parent._root
            self.path  = parent.path + "." + self.name
        else:
            self._root = parent
            self.path  = parent.name + "." + self.name

        # Add to parent list
        if hasattr(self._parent,self.name):
            raise NodeError('Invalid %s name %s. Name already exists' % (self.classType,self.name))
        else:
            setattr(self._parent,self.name,self)


class Variable(Node):
    """Variable holder"""

    def __init__(self, parent, name, description, bitSize=32, bitOffset=0, 
                 base='hex', mode='RW', enums=None, hidden=False, setFunction=None, getFunction=None):
        """Initialize variable class"""

        Node.__init__(self,parent,name,description,hidden,'variable')

        # Public Attributes
        self.bitSize   = bitSize
        self.bitOffset = bitOffset
        self.base      = base      
        self.mode      = mode
        self.enums     = enums
        self.hidden    = hidden

        # Check modes
        if (self.mode != 'RW') and (self.mode != 'RO') and (self.mode != 'WO') and (self.mode != 'CMD'):
            raise VariableError('Invalid variable mode %s. Supported: RW, RO, WO, CMD' % (self.mode))

        # Tracking variables
        self._block       = None
        self._setFunction = setFunction
        self._getFunction = getFunction

    def _intSet(self,value):
        if self.mode == 'RW' or self.mode == 'WO' or self.mode == 'CMD':
            if self._setFunction != None:
                if callable(self._setFunction):
                    self._setFunction(self,value)
                else:
                    exec(textwrap.dedent(self._setFunction))

            elif self._block:        
                if self.base == 'string':
                    self._block.setString(value)
                else:
                    self._block.setUInt(self.bitOffset,self.bitSize,value)

            self._updated()
        else:
            raise VariableError('Attempt to set variable with mode %s' % (self.mode))
                
    def _intGet(self):
        if self.mode == 'RW' or self.mode == 'RO':
            if self._getFunction != None:
                if callable(self._getFunction):
                    return(self._getFunction(self))
                else:
                    value = None
                    exec(textwrap.dedent(self._getFunction))
                    return value
   
            elif self._block:        
                if self.base == 'string':
                    return(self._block.getString())
                else:
                    return(self._block.getUInt(self.bitOffset,self.bitSize))
            else:
                return None
        else:
            raise VariableError('Attempt to get variable with mode %s' % (self.mode))

    def _updated(self):
        """Placeholder for calling listeners when variable is updated"""
        pass

    def set(self,value):
        """Set a value without writing to hardware"""
        self._intSet(value)

    def write(self,value):
        """Set a value with write to hardware"""
        self._intSet(value)
        if self._block: self._block.blockingWrite()

    def writePosted(self,value):
        """Set a value with posted write to hardware"""
        self._intSet(value)
        if self._block: self._block.postedWrite()

    def get(self):
        """Get a value from shadow memory"""
        return self._intGet()

    def read(self):
        """Get a value after read from hardware"""
        if self._block: self._block.blockingRead()
        return self._intGet()


class Command(Node):
    """Command holder"""

    def __init__(self, parent, name, description, function=None, hidden=False):
        """Initialize command class"""

        Node.__init__(self,parent,name,description,hidden,'command')

        # Public attributes
        self.hidden = hidden

        # Tracking
        self._function = function

    def __call__(self,arg=None):
        """Execute command"""

        if self._function != None:
            if callable(self._function):
                self._function(self,arg)
            else:
                exec(textwrap.dedent(self._function))


class Block(rogue.interfaces.memory.Block):
    """Memory block holder"""

    def __init__(self,parent,offset,size,variables,pollEn=False):
        """Initialize memory block class"""
        rogue.interfaces.memory.Block.__init__(self,offset,size)

        self.variables = variables
        self.pollEn    = pollEn
        self.mode      = 'RO'

        # Add to device tracking list
        parent._blocks.append(self)

        # Update position in memory map
        self._inheritFrom(parent)

        for variable in variables:
            variable._block = self

            # Determine block mode based upon variables
            # mismatch assumes block is read/write
            if self.mode == '':
                self.mode = variable.mode
            elif self.mode != variable.mode:
                self.mode = 'RW'

    # Generate variable updates if block has been updated
    def _checkUpdated(self):
        if block.getUpdated():
            for variable in self.variables:
                variable._updated()


class Device(Node,rogue.interfaces.memory.Master):
    """Device class holder"""

    def __init__(self, parent, name, description, size, memBase=None, offset=0, hidden=False):
        """Initialize device class"""

        Node.__init__(self,parent,name,description,hidden,'device')
        rogue.interfaces.memory.Master.__init__(self,offset,size)

        # Blocks
        self._blocks = []
        self._enable = True

        # Adjust position in tree
        if memBase:
            self._setMemBase(memBase,offset)
        elif isinstance(self._parent,rogue.interfaces.memory.Master):
            self._inheritFrom(self._parent)

        # Variable interface to enable flag
        Variable(parent=self, name='enable', description='Enable Flag', bitSize=1, 
                bitOffset=0, base='bool', mode='RW', setFunction=self._setEnable, getFunction=self._getEnable)

    def _setEnable(self, var, enable):
        self._enable = enable

        for block in self._blocks:
            block.setEnable(enable)

    def _getEnable(self,var):
        return(self._enable)

    def _setMemBase(self,memBase,offset=None):
        """Connect to memory slave at offset"""
        if offset: self._setAddress(offset)

        # Membase is a Device
        if isinstance(memBase,Device):
            # Inhertit base address and slave pointer from one level up
            self._inheritFrom(memBase)

        # Direct connection to slae
        else:
            self._setSlave(memBase)

        # Adust address map in blocks
        for block in self._blocks:
            block._inheritFrom(self)

        # Adust address map in sub devices
        for key,dev in self.__dict__.iteritems():
            if isinstance(dev,Device):
                dev._setMemBase(self)

    def _readOthers(self):
        """Method to read and update non block based variables. Called from readAllBlocks."""
        # Be sure to call variable._updated() on variables to propogate changes to listeners
        pass

    def _pollOthers(self):
        """Method to poll and update non block based variables. Called from pollAllBlocks."""
        # Be sure to call variable._updated() on variables to propogate changes to listeners
        pass


def _getStructure(obj):
    """Recursive function to generate a dictionary for the structure"""
    data = {}
    for key,value in obj.__dict__.iteritems():
       if not callable(value) and not key.startswith('_'):
          if isinstance(value,Node):
              data[key] = _getStructure(value)
          else:
              data[key] = value

    return data


def _getConfig(obj):
    """Recursive function to generate a dictionary for the configuration"""
    data = {}
    for key,value in obj.__dict__.iteritems():
        if isinstance(value,Device):
            data[key] = _getConfig(value)
        elif isinstance(value,Variable) and value.mode=='RW':
            data[key] = value.get()

    return data


def _setConfig(obj,d):
    """Recursive function to set configuration from a dictionary"""
    for key, value in d.iteritems():
        v = getattr(obj,key)
        if isinstance(v,Device):
            _setConfig(v,value)
        elif isinstance(v,Variable):
            v.set(value)


def _writeAll(obj):
    """Recursive function to write all blocks in each Device"""
    for key,value in obj.__dict__.iteritems():
        if isinstance(value,Device):
            for block in value._blocks:
                if block.mode == 'WO' or block.mode == 'RW':
                    block.backgroundWrite()
            _writeAll(value)


def _writeStale(obj):
    """Recursive function to write stale blocks in each Device"""
    for key,value in obj.__dict__.iteritems():
        if isinstance(value,Device):
            for block in value._blocks:
                if block.getStale() and (block.mode == 'WO' or block.mode == 'RW'):
                    block.backgroundWrite()
            _writeStale(value)


def _readAll(obj):
    """Recursive function to read all of the blocks in each Device"""
    for key,value in obj.__dict__.iteritems():
        if isinstance(value,Device):
            for block in value._blocks:
                if block.mode == 'RO' or block.mode == 'RW':
                    block.backgroundRead()

            value._readOthers()
            _readAll(value)


def _readPollable(obj):
    """Recursive function to read pollable blocks in each Device"""
    for key,value in obj.__dict__.iteritems():
        if isinstance(value,Device):
            for block in value._blocks:
                if block.pollEn and (block.mode == 'RO' or block.mode == 'RW'):
                    block.backgroundRead()

            value._pollOthers()
            _readPollable(value)


def _checkUpdatedBlocks(obj):
    """Recursive function to check status of all blocks in each Device"""
    for key,value in obj.__dict__.iteritems():
        if isinstance(value,Device):
            for block in value._blocks:
                block._checkUpdated()

            _checkUpdatedBlocks(value)


def _getAtPath(obj,path):
    """Recursive function to return object at passed path"""
    if not '.' in path:
        return getattr(obj,path)
    else:
        base = path[:path.find('.')]
        rest = path[path.find('.')+1:]
        return(_getAtPath(getattr(obj,base),rest))


