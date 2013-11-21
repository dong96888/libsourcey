//
// LibSourcey
// Copyright (C) 2005, Sourcey <http://sourcey.com>
//
// LibSourcey is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// LibSourcey is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//


#include "scy/packetstream.h"
#include "scy/packetqueue.h"
#include "scy/memory.h"


using std::endl;


namespace scy {


PacketStream::PacketStream(const std::string& name) : 
	_clientData(nullptr),
	_name(name)
{
	traceL("PacketStream", this) << "Create" << endl;
	
	//_base = std::make_shared<PacketStreamBase>(this);
	_base = std::shared_ptr<PacketStreamBase>(new PacketStreamBase(this), 
		std::default_delete<PacketStreamBase>()
		// NOTE: No longer using GC for deleting PacketStreamBase 
		// since we don't want to force use of the event loop.
		//deleter::Deferred<PacketStreamBase>()
		);
}


PacketStream::~PacketStream()
{
	traceL("PacketStream", this) << "Destroy" << endl;

	close();
			
	// Delete managed adapters
	_base->cleanup();

	// Nullify the stream pointer 
	_base->setStream(nullptr);

	traceL("PacketStream", this) << "Destroy: OK" << endl;
}


void PacketStream::start()
{	
	traceL("PacketStream", this) << "Start" << endl;
	
	//Mutex::ScopedLock lock(_mutex);
	if (_base->stateEquals(PacketStreamState::Active)) {
		traceL("PacketStream", this) << "Start: Already active" << endl;
		//assert(0);
		return;
	}
	
	// Setup the delegate chain
	_base->setup();

	// Setup default (thread-based) runner if none set yet
	//if (!_base->_runner)
	//	setRunner(std::make_shared<Thread>());
		
	// Set state to Active
	_base->setState(this, PacketStreamState::Active);
	
	// Lock the processor mutex to synchronize multi source streams
	Mutex::ScopedLock lock(_base->_procMutex);

	// Start synchronized sources
	_base->startSources();
}


void PacketStream::stop()
{
	traceL("PacketStream", this) << "Stop" << endl;
	
	//Mutex::ScopedLock lock(_mutex);
	if (_base->stateEquals(PacketStreamState::Stopped) ||
		_base->stateEquals(PacketStreamState::Stopping) ||
		_base->stateEquals(PacketStreamState::Closed)) {
		traceL("PacketStream", this) << "Stop: Already stopped" << endl;
		//assert(0);
		return;
	}

	_base->setState(this, PacketStreamState::Stopping);		
	_base->setState(this, PacketStreamState::Stopped);
	
	// Lock the processor mutex to synchronize multi source streams
	Mutex::ScopedLock lock(_base->_procMutex);

	// Stop synchronized sources
	_base->stopSources();

	traceL("PacketStream", this) << "Stop: OK" << endl;
}


void PacketStream::pause()
{
	traceL("PacketStream", this) << "Pause" << endl;
	//Mutex::ScopedLock lock(_mutex);
	_base->setState(this, PacketStreamState::Paused);
}


void PacketStream::resume()
{
	traceL("PacketStream", this) << "Resume" << endl;
	//Mutex::ScopedLock lock(_mutex);
	if (!_base->stateEquals(PacketStreamState::Paused)) {
		traceL("PacketStream", this) << "Resume: Not paused" << endl;
		return;
	}
	
	_base->setState(this, PacketStreamState::Active);
}


void PacketStream::reset()
{
	traceL("PacketStream", this) << "Reset" << endl;
	
	//Mutex::ScopedLock lock(_mutex);
	_base->setState(this, PacketStreamState::Resetting);
	_base->setState(this, PacketStreamState::Active);
}


void PacketStream::close()
{
	traceL("PacketStream", this) << "Close" << endl;

	//Mutex::ScopedLock lock(_mutex);
	if (_base->stateEquals(PacketStreamState::None) ||
		_base->stateEquals(PacketStreamState::Closed)) {
		traceL("PacketStream", this) << "Already closed" << endl;
		//assert(0);
		return;
	}
	
	// Stop the stream gracefully (if running)
	if (!_base->stateEquals(PacketStreamState::Stopped) &&
		!_base->stateEquals(PacketStreamState::Stopping))
		stop();

	// Queue the Closed state
	_base->setState(this, PacketStreamState::Closed);
	
	{
		// Lock the processor mutex to synchronize multi source streams
		Mutex::ScopedLock lock(_base->_procMutex);

		// Teardown the adapter delegate chain
		traceL("PacketStream", this) << "Destroy: Teardown: " << _base << endl;
		_base->teardown();

		// Wait for thread-based runners to stop running in order
		// to ensure safe destruction of stream adapters. 
		// This call does nothing for non thread-based runners.
		//_base->waitForRunner();
	
		// Synchronize any pending states
		// This should be safe since the adapters won't be receiving
		// any more incoming packets after teardown.
		// This call is essential when using the event loop otherwise
		// failing to close some handles could result in deadlock.
		// See SyncQueue::cancel()
		_base->synchronizeStates();
	}

	// Send the Closed signal
	Close.emit(this);
	
	traceL("PacketStream", this) << "Close: OK" << endl;
}


void PacketStream::attachSource(PacketStreamAdapter* source, bool freePointer, bool syncState)
{
	//Mutex::ScopedLock lock(_mutex);
	_base->attachSource(source, freePointer, syncState);
}


void PacketStream::attachSource(PacketSignal& source)
{
	//Mutex::ScopedLock lock(_mutex);
	_base->attachSource(new PacketStreamAdapter(source), true, false);
}


bool PacketStream::detachSource(PacketStreamAdapter* source) 
{
	//Mutex::ScopedLock lock(_mutex);
	return _base->detachSource(source);
}


bool PacketStream::detachSource(PacketSignal& source) 
{
	//Mutex::ScopedLock lock(_mutex);
	return _base->detachSource(source);
}


void PacketStream::attach(PacketProcessor* proc, int order, bool freePointer) 
{
	//Mutex::ScopedLock lock(_mutex);
	_base->attach(proc, order, freePointer);
}


bool PacketStream::detach(PacketProcessor* proc) 
{
	//Mutex::ScopedLock lock(_mutex);
	return _base->detach(proc);
}


void PacketStream::write(char* data, std::size_t len)
{
	//Mutex::ScopedLock lock(_mutex);
	 RawPacket p(data, len);
	_base->process(p);
}


void PacketStream::write(const char* data, std::size_t len)
{
	//Mutex::ScopedLock lock(_mutex);
	 RawPacket p(data, len);
	_base->process(p);
}
	

void PacketStream::write(IPacket& packet)
{
	//Mutex::ScopedLock lock(_mutex);
	_base->process(packet);
}


void PacketStream::synchronizeOutput(uv::Loop* loop)
{
	//Mutex::ScopedLock lock(_mutex);
	_base->synchronizeOutput(loop);
}


bool PacketStream::locked() const
{
	//Mutex::ScopedLock lock(_mutex);
	return _base->stateEquals(PacketStreamState::Locked);
}


//bool PacketStream::async() const
//{
//	return false; //_base->_runner && _base->_runner->async();
//}


bool PacketStream::lock()
{
	//Mutex::ScopedLock lock(_mutex);
	if (!_base->stateEquals(PacketStreamState::None))
		return false;

	_base->setState(this, PacketStreamState::Locked);
	return true;
}
	
	
bool PacketStream::active() const
{
	//Mutex::ScopedLock lock(_mutex);
	return _base->stateEquals(PacketStreamState::Active);
}

	
bool PacketStream::closed() const
{
	//Mutex::ScopedLock lock(_mutex);
	return _base->stateEquals(PacketStreamState::Closed)
		|| _base->stateEquals(PacketStreamState::Error);
}

	
bool PacketStream::stopped() const
{
	//Mutex::ScopedLock lock(_mutex);
	return _base->stateEquals(PacketStreamState::Stopping) 
		|| _base->stateEquals(PacketStreamState::Stopped);
}
	

void PacketStream::setClientData(void* data)
{
	Mutex::ScopedLock lock(_mutex);
	_clientData = data;
}


void* PacketStream::clientData() const
{
	Mutex::ScopedLock lock(_mutex);
	return _clientData;
}
	

void PacketStream::closeOnError(bool flag)
{
	//Mutex::ScopedLock lock(_mutex);
	_base->_closeOnError = flag;
}


std::string PacketStream::name() const
{
	Mutex::ScopedLock lock(_mutex);	
	return _name;
}


const std::exception_ptr& PacketStream::error()
{
	//Mutex::ScopedLock lock(_mutex);
	return _base->error();
}


PacketStreamBase& PacketStream::base() const
{
	Mutex::ScopedLock lock(_mutex);
	if (!_base)
		throw std::runtime_error("Packet stream context not initialized.");
	return *_base;
}


//
// Packet Stream Base
//
	

PacketStreamBase::PacketStreamBase(PacketStream* stream) : 
	_stream(stream),
	_closeOnError(false)
{
	traceL("PacketStreamBase", this) << "Create" << endl;
}


PacketStreamBase::~PacketStreamBase()
{
	traceL("PacketStreamBase", this) << "Destroy" << endl;
	
	// The event machine should always be complete
	assert(stateEquals(PacketStreamState::None)
		|| stateEquals(PacketStreamState::Closed)
		|| stateEquals(PacketStreamState::Error));

	// Make sure all adapters have been cleaned up
	assert(_sources.empty());
	assert(_processors.empty());

	traceL("PacketStreamBase", this) << "Destroy: OK" << endl;
}


void PacketStreamBase::synchronizeStates()
{		
	// Process queued internal states first
	while (!_states.empty()) {
		PacketStreamState state;
		{
			Mutex::ScopedLock lock(_mutex);
			state = _states.front();
			_states.pop_front();
		}	

		traceL("PacketStreamBase", this) << "Set queued state: " << state << endl;
		//setState(this, id);
		
		// Send the stream state to packet adapters.
		// This is done inside the processor thread context so  
		// packet adapters do not need to consider thread safety.
		auto adapters = this->adapters();
		for (auto& ref : adapters) {
			auto adapter = dynamic_cast<PacketStreamAdapter*>(ref.ptr);
			traceL("PacketStreamBase", this) << "Set queued state to adapter: " << adapter << endl;
			if (adapter)
				adapter->onStreamStateChange(state);
			else assert(0);
		}
	}
}


void PacketStreamBase::process(IPacket& packet)
{	
	//traceL("PacketStreamBase", this) << "Processing packet: " 
	//	<< state() << ": " << packet.className() << endl;	
	//assert(Thread::currentID() == _runner->tid());

	try {	

		// Process the packet if the stream is active
		PacketProcessor* firstProc = nullptr;
		if (stateEquals(PacketStreamState::Active) && !packet.flags.has(PacketFlags::NoModify)) {
			{
				Mutex::ScopedLock lock(_mutex);
				firstProc = !_processors.empty() ? 
					reinterpret_cast<PacketProcessor*>(_processors[0].ptr) : nullptr;
			}			
			if (firstProc) {
				
				// Lock the processor mutex to synchronize multi source streams
				Mutex::ScopedLock lock(_procMutex);

				// Sync queued states
				synchronizeStates();

				// Send the packet to the first processor in the chain
				if (firstProc->accepts(packet)) {
					//traceL("PacketStreamBase", this) << "Starting process chain: " 
					//	<< firstProc << ": " << packet.className() << endl;
					assert(stateEquals(PacketStreamState::Active));
					firstProc->process(packet);
					// If all went well the packet was processed and emitted...
				}
				
				// Proxy packets which are rejected by the first processor
				else {
					warnL("PacketStreamBase", this) << "Source packet rejected: " 
						<< firstProc << ": " << packet.className() << endl;
					firstProc = nullptr;
				}			
			}
		}

		// Otherwise just proxy and emit the packet
		if (!firstProc) {
			//traceL("PacketStreamBase", this) << "Proxying: " 
			//	<< state() << ": " << packet.className() << endl;
			emit(packet);
		}
	}
				
	// Catch any exceptions thrown within the processor  
	catch (std::exception& exc) {
		errorL("PacketStreamBase", this) << "Processor error: " << exc.what() << endl;
		
		// Set the stream Error state. No need for queueState
		// as we are currently inside the processor context.
		setState(this, PacketStreamState::Error, exc.what());
		
		// Capture the exception so it can be rethrown elsewhere.
		// The Error signal will be sent on next call to emit()
		_error = std::current_exception();
		stream()->Error.emit(stream(), _error);

		//_syncError = true;
		if (_closeOnError) {
			traceL("PacketStreamBase", this) << "Close on error" << endl;
			stream()->close();
		}
	}	
	
	//traceL("PacketStreamBase", this) << "End process chain: " 
	//	<< state() << ": " << packet.className() << endl;	
}


//void PacketStreamBase::sync(IPacket& packet)
//{
//}


void PacketStreamBase::emit(IPacket& packet)
{
	traceL("PacketStreamBase", this) << "Emit: " << packet.size() << endl;

	PacketStream* stream = this->stream();
	const PacketStreamState& state = this->state();

	/*
	// Synchronize the error if required
	if (_syncError && state.equals(PacketStreamState::Error)) {
		_syncError = false;
		if (stream)
			stream->Error.emit(stream, error());

		if (_closeOnError) {
			traceL("PacketStreamBase", this) << "Close on error" << endl;
			stream->close();
			return;
		}
	}
	*/

	// Ensure the stream is still running
	if (!state.equals(PacketStreamState::Active)) {
		debugL("PacketStreamBase", this) << "Dropping late packet: " 
			<< state << endl;
		return;
	}

	// Emit the result packet
	if (stream && 
		stream->emitter.enabled()) {
		stream->emitter.emit(stream, packet);	
	}
	else
		debugL("PacketStreamBase", this) << "Dropping packet: No emitter: " 
			<< stream << ": " << state << endl;
}


void PacketStreamBase::setup()
{
	try {	
		Mutex::ScopedLock lock(_mutex);		
		
		// Setup the processor chain
		PacketProcessor* lastProc = nullptr;
		PacketProcessor* thisProc = nullptr;
		for (auto& proc : _processors) {
			thisProc = reinterpret_cast<PacketProcessor*>(proc.ptr);
			if (lastProc) {
				lastProc->getEmitter().attach(packetDelegate(thisProc, &PacketProcessor::process));
			}
			lastProc = thisProc;
		}

		// The last processor will emit the packet to the application
		if (lastProc)
			lastProc->getEmitter().attach(packetDelegate(this, &PacketStreamBase::emit));

		// Attach source emitters to the PacketStreamBase::process method
		for (auto& source : _sources) {
			source.ptr->getEmitter().attach(packetDelegate(this, &PacketStreamBase::process));
		}	
	}
	catch (std::exception& exc) {
		errorL("PacketStream", this) << "Cannot start stream: " << exc.what() << endl;
		setState(this, PacketStreamState::Error, exc.what());
		throw exc;
	}
}


void PacketStreamBase::teardown()
{
	traceL("PacketStreamBase", this) << "Teardown" << endl;
			
	Mutex::ScopedLock lock(_mutex);		
	traceL("PacketStreamBase", this) << "Stopping: Detach" << endl;

	// Detach the processor chain first
	PacketProcessor* lastProc = nullptr;
	PacketProcessor* thisProc = nullptr;
	for (auto& proc : _processors) {
		thisProc = reinterpret_cast<PacketProcessor*>(proc.ptr);
		if (lastProc)
			lastProc->getEmitter().detach(packetDelegate(thisProc, &PacketProcessor::process));
		lastProc = thisProc;
	}
	if (lastProc)
		lastProc->getEmitter().detach(packetDelegate(this, &PacketStreamBase::emit));

	// Detach sources
	for (auto& source : _sources) {
		source.ptr->getEmitter().detach(packetDelegate(this, &PacketStreamBase::process));
	}

	traceL("PacketStreamBase", this) << "Teardown: OK" << endl;
}


void PacketStreamBase::cleanup()
{
	traceL("PacketStreamBase", this) << "Cleanup" << endl;		
	
	assert(stateEquals(PacketStreamState::None)
		|| stateEquals(PacketStreamState::Closed));

	Mutex::ScopedLock lock(_mutex);
	auto sit = _sources.begin();
	while (sit != _sources.end()) {
		traceL("PacketStreamBase", this) << "Remove source: " 
			<< (*sit).ptr << ": " << (*sit).freePointer << endl;
		if ((*sit).freePointer) {
//#ifdef _DEBUG			
			delete (*sit).ptr;
//#else
//			deleteLater<PacketStreamAdapter>((*sit).ptr);
//#endif
		}
		sit = _sources.erase(sit);
	}	
	
	auto pit = _processors.begin();
	while (pit != _processors.end()) {
		traceL("PacketStreamBase", this) << "Remove processor: " 
			<< (*pit).ptr << ": " << (*pit).freePointer << endl;
		if ((*pit).freePointer) {
//#ifdef _DEBUG
			delete (*pit).ptr;
//#else
//			deleteLater<PacketStreamAdapter>((*pit).ptr);
//#endif
		}
		pit = _processors.erase(pit);
	}

	traceL("PacketStreamBase", this) << "Cleanup: OK" << endl;		
}


void PacketStreamBase::attachSource(PacketStreamAdapter* source, bool freePointer, bool syncState)
{
	//traceL("PacketStreamBase", this) << "Attach source: " << source << endl;	
	assertNotActive();

	Mutex::ScopedLock lock(_mutex);
	_sources.push_back(PacketAdapterReference(source, 0, freePointer, syncState));
	std::sort(_sources.begin(), _sources.end(), PacketAdapterReference::compareOrder);
}


void PacketStreamBase::attachSource(PacketSignal& source)
{
	//traceL("PacketStreamBase", this) << "Attach source signal: " << &source << endl;	
	assertNotActive();
	
	// TODO: unique_ptr for exception safe pointer creation so we
	// don't need to do state checks here as well as attachSource(PacketStreamAdapter*)
	attachSource(new PacketStreamAdapter(source), true, false);
}


bool PacketStreamBase::detachSource(PacketStreamAdapter* source) 
{
	//traceL("PacketStreamBase", this) << "Detach source adapter: " << source << endl;	
	assertNotActive();

	Mutex::ScopedLock lock(_mutex);
	for (auto it = _sources.begin(); it != _sources.end(); ++it) {
		if ((*it).ptr == source) {
			(*it).ptr->getEmitter().detach(packetDelegate(stream(), &PacketStream::write));
			traceL("PacketStreamBase", this) << "Detached source adapter: " << source << endl;
			
			// Note: The PacketStream is no longer responsible
			// for deleting the managed pointer.
			_sources.erase(it);
			return true;
		}
	}
	return false;
}


bool PacketStreamBase::detachSource(PacketSignal& source) 
{
	//traceL("PacketStreamBase", this) << "Detach source signal: " << &source << endl;
	assertNotActive();

	Mutex::ScopedLock lock(_mutex);
	for (auto it = _sources.begin(); it != _sources.end(); ++it) {
		if (&(*it).ptr->getEmitter() == &source) {
			(*it).ptr->getEmitter().detach(packetDelegate(stream(), &PacketStream::write));
			traceL("PacketStreamBase", this) << "Detached source signal: " << &source << endl;

			// Free the PacketStreamAdapter wrapper instance,
			// not the referenced PacketSignal.
			assert((*it).freePointer);
			delete (*it).ptr;
			_sources.erase(it);
			return true;
		}
	}
	return false;
}


void PacketStreamBase::attach(PacketProcessor* proc, int order, bool freePointer) 
{
	//traceL("PacketStreamBase", this) << "Attach processor: " << proc << endl;
	assert(order >= 0 && order <= 101);
	assertNotActive();

	Mutex::ScopedLock lock(_mutex);
	_processors.push_back(PacketAdapterReference(proc, order == 0 ? _processors.size() : order, freePointer));
	sort(_processors.begin(), _processors.end(), PacketAdapterReference::compareOrder);
}


bool PacketStreamBase::detach(PacketProcessor* proc) 
{
	//traceL("PacketStreamBase", this) << "Detach processor: " << proc << endl;
	assertNotActive();

	Mutex::ScopedLock lock(_mutex);
	for (auto it = _processors.begin(); it != _processors.end(); ++it) {
		if ((*it).ptr == proc) {
			traceL("PacketStreamBase", this) << "Detached processor: " << proc << endl;

			// Note: The PacketStream is no longer responsible
			// for deleting the managed pointer.
			_processors.erase(it);
			return true;
		}
	}
	return false;
}


void PacketStreamBase::startSources() 
{
	//Mutex::ScopedLock lock(_mutex);
	auto sources = this->sources();
	for (auto& source : sources) {	
		if (source.syncState) {				
			auto startable = dynamic_cast<async::Startable*>(source.ptr);
			if (startable) {
				traceL("PacketStreamBase", this) << "Start source: " << startable << endl;
				startable->start();
			}
			else assert(0 && "unknown synchronizable");
#if 0
			auto runnable = dynamic_cast<async::Runnable*>(source);
			if (runnable) {
				traceL("PacketStream", this) << "Starting runnable: " << source << endl;
				runnable->run();
			}
#endif
		}
	}
}


void PacketStreamBase::stopSources()
{
	//Mutex::ScopedLock lock(_mutex);
	auto sources = this->sources();
	for (auto& source : sources) {	
		if (source.syncState) {
			auto startable = dynamic_cast<async::Startable*>(source.ptr);
			if (startable) {
				traceL("PacketStreamBase", this) << "Stop source: " << startable << endl;
				startable->stop();
			}
			else assert(0 && "unknown synchronizable");
#if 0
			auto runnable = dynamic_cast<async::Runnable*>(source);
			if (runnable) {
				traceL("PacketStream", this) << "Stop runnable: " << source << endl;
				runnable->cancel();
			}
#endif
		}
	}
}


bool PacketStreamBase::waitForRunner()
{	
	/*
	traceL("PacketStreamBase", this) << "Wait for sync: " 
		<< times << ": "
		<< !_runner->cancelled() << ": "
		<< _runner->running()
		<< endl;

	if (!_runner || !_runner->async())
		return false;

	//assert(Thread::currentID() != _runner->tid());
	
	traceL("PacketStreamBase", this) << "Wait for sync" << endl;
	int times = 0;
	while (!_runner->cancelled() || _runner->running()) { //!_runner->cancelled() || 
		traceL("PacketStreamBase", this) << "Wait for sync: " 
			<< times << ": "
			<< !_runner->cancelled() << ": "
			<< _runner->running()
			<< endl;
		scy::sleep(10);
		if (times++ > 500) {
			assert(0 && "deadlock; calling inside stream scope?"); // 5 secs
		}
	}
		*/
	
	traceL("PacketStreamBase", this) << "Wait for sync: OK" << endl;
	return true;
}


bool PacketStreamBase::waitForStateSync(PacketStreamState::ID state)
{
	int times = 0;
	traceL("PacketStreamBase", this) << "Wait for sync state: " << state << endl;
	while (!stateEquals(state) || hasQueuedState(state)) {
		traceL("PacketStreamBase", this) << "Wait for sync state: " << state << ": " << times << endl;
		scy::sleep(10);
		if (times++ > 500) {
			assert(0 && "deadlock; calling inside stream scope?"); // 5 secs
		}
	}
	traceL("PacketStreamBase", this) << "Wait for sync state: " << state << ": OK" << endl;
	return true;
}


bool PacketStreamBase::hasQueuedState(PacketStreamState::ID state) const
{
	Mutex::ScopedLock lock(_mutex);
	for (auto const& st : _states) {
		if (st.id() == state) 
			return true;
	}
	return false;
}


void PacketStreamBase::assertNotActive()
{
	if (stateEquals(PacketStreamState::Active)) {
		assert(0 && "cannot modify active stream");
		throw std::runtime_error("Stream error: Cannot modify an active stream.");
	}
}


void PacketStreamBase::synchronizeOutput(uv::Loop* loop)
{
	assertNotActive();
	
	// Add a SyncPacketQueue as the final processor so output 
	// packets will be synchronized when they hit the emit() method
	attach(new SyncPacketQueue(loop), 101, true);
}


void PacketStreamBase::onStateChange(PacketStreamState& state, const PacketStreamState& oldState)
{	
	traceL("PacketStreamBase", this) << "On state change: " << oldState << " => " << state << endl;
	
	// Queue state for passing to adapters 
	Mutex::ScopedLock lock(_mutex);
	_states.push_back(state);
}


const std::exception_ptr& PacketStreamBase::error()
{
	Mutex::ScopedLock lock(_mutex);
	return _error;
}


int PacketStreamBase::numSources() const
{
	Mutex::ScopedLock lock(_mutex);
	return _sources.size();
}


int PacketStreamBase::numProcessors() const
{
	Mutex::ScopedLock lock(_mutex);
	return _processors.size();
}


int PacketStreamBase::numAdapters() const
{
	Mutex::ScopedLock lock(_mutex);
	return _sources.size() + _processors.size();
}


PacketAdapterVec PacketStreamBase::adapters() const
{
	Mutex::ScopedLock lock(_mutex);
	PacketAdapterVec res(_sources);
	res.insert(res.end(), _processors.begin(), _processors.end());
	return res;
}


PacketAdapterVec PacketStreamBase::sources() const
{
	Mutex::ScopedLock lock(_mutex);
	return _sources;
}


PacketAdapterVec PacketStreamBase::processors() const
{
	Mutex::ScopedLock lock(_mutex);
	return _processors;
}


PacketStream* PacketStreamBase::stream() const
{
	Mutex::ScopedLock lock(_mutex);
	return _stream;
}


void PacketStreamBase::setStream(PacketStream* stream)
{
	Mutex::ScopedLock lock(_mutex);
	assert(!_stream || stream == nullptr);
	_stream = stream;
}


//
// Packet Stream Adapter
//


PacketStreamAdapter::PacketStreamAdapter(PacketSignal& emitter) :
	_emitter(emitter)
{
}


void PacketStreamAdapter::emit(char* data, std::size_t len, unsigned flags)
{
	RawPacket p(data, len, flags);
	emit(p);
}


void PacketStreamAdapter::emit(const char* data, std::size_t len, unsigned flags)
{
	RawPacket p(data, len, flags);
	emit(p);
}


void PacketStreamAdapter::emit(const std::string& str, unsigned flags)
{
	RawPacket p(str.c_str(), str.length(), flags);
	emit(p);
}


void PacketStreamAdapter::emit(IPacket& packet)
{
	getEmitter().emit(this, packet);
}


PacketSignal& PacketStreamAdapter::getEmitter()
{
	return _emitter;
}


} // namespace scy


	//assert((!_runner || _runner->async()) && "runner already synchronized");

	/*
	// Handle stream states before they are proxied to adapters
	switch (state.id()) {
	//case PacketStreamState::None:
	//case PacketStreamState::Locked:
	//case PacketStreamState::Active:
	//case PacketStreamState::Paused:
	//case PacketStreamState::Resetting:
	//case PacketStreamState::Stopping:
	//case PacketStreamState::Stopped:
	//case PacketStreamState::Error:	
	case PacketStreamState::Closed:		
	
		// Detach the processor chain.
		//teardown();

		// Cancel the Runner to reset the async contect and decrement  
		// the PacketStreamBase shared_ptr
		//if (_runner)
		//	_runner->cancel();
		
		// Cancel the RunnableQueue so the Runner context returns ASAP
		//RunnableQueue<IPacket>::cancel();
		break;
	}
	*/

	/*
	if (Thread::currentID() != _runner->tid()) {
		assert(0);
		throw std::runtime_error("Wait for state sync inside stream scope will cause deadlock.");
	}
	*/



/*
void PacketStream::setRunner(async::Runner::ptr runner)
{
	traceL("PacketStream", this) << "Set async context: " << runner << endl;
	//Mutex::ScopedLock lock(_mutex);
	_base->assertNotActive();
	assert(!_base->_runner);
	
	// Ensure a reasonable timeout is set when using an event 
	// loop based runner; we don't block the loop for too long.
	if (!runner->async() && _base->timeout() == 0)
		_base->setTimeout(20); 
	
	// Note: std::bind will increment the PacketStreamBase std::shared_ptr
	// ref count, so the PacketStreamBase will not be destroyed until
	// the Runner::Context::reset() is called (or the function is unbound).
	_base->_runner = runner;
	_base->_runner->start(std::bind(&RunnableQueue<IPacket>::run, _base));
}
*/

	/*
	// Return if already closed
	if (_base->stateEquals(PacketStreamState::Closed)) {
		traceL("PacketStream", this) << "Already closed" << endl;
		//assert(0);
		return;
	}
		
	// Run close sequence if the stream is active
	if (!_base->stateEquals(PacketStreamState::None)) {

		// Stop the stream gracefully (if running)
		if (!_base->stateEquals(PacketStreamState::Stopped) &&
			!_base->stateEquals(PacketStreamState::Stopping))
			stop();

		// Queue the Closed state
		_base->setState(this, PacketStreamState::Closed);

		// Send the Closed signal
		Close.emit(this);
	}
	*/	

/*


void PacketStreamBase::write(IPacket& packet)
{	
	//Mutex::ScopedLock lock(_mutex);

	// Clone and push onto the processor queue	
	process(packet); //.clone()
}
bool PacketStreamBase::dispatchNext()
{
	// Process queued internal states first
	while (!_states.empty()) {
		PacketStreamState state;
		{
			Mutex::ScopedLock lock(_mutex);
			state = _states.front();
			_states.pop_front();
		}	

		traceL("PacketStreamBase", this) << "Set queued state: " << state << endl;
		//setState(this, id);
		
		// Send the stream state to packet adapters.
		// This is done inside the processor thread context so  
		// packet adapters do not need to consider thread safety.
		//PacketStreamState state;
		//state.set(id);
		auto adapters = this->adapters();
		for (auto& ref : adapters) {
			auto adapter = dynamic_cast<PacketStreamAdapter*>(ref.ptr);
			traceL("PacketStreamBase", this) << "Set queued state to adapter: " << adapter << endl;
			if (adapter)
				adapter->onStreamStateChange(state);
			else assert(0);
		}
	}

	// Process packets if the stream is running
	return RunnableQueue<IPacket>::dispatchNext();
}
*/

	/*
	int times = 0;
	traceL("PacketStream", this) << "Wait for sync" << endl;
	while (!_queue.empty() || !_states.empty()) { // || !_syncStates.empty()
		traceL("PacketStream", this) << "Wait queue: " 
			<< _queue.size() << ": "
			<< _states.size() //<< ": "
			//<< _syncStates.size() 
			<< endl;
		scy::sleep(10);
		if (times++ > 500) {
			assert(0 && "deadlock; calling inside stream scope?"); // 5 secs
		}
	}
	*/

/*
void PacketStreamBase::setState(this, PacketStreamState::ID state)
{
	Mutex::ScopedLock lock(_mutex);
	_states.push_back(state);
}
*/
		
	/*
	// Send the stream state to packet adapters.
	// This is done inside the processor thread context so  
	// packet adapters do not need to consider thread safety.
	//PacketStreamState state;
	//state.set(id);
	auto adapters = this->adapters();
	traceL("PacketStreamBase", this) << "On state change: Adapters: " << adapters.size() << endl;
	for (auto& ref : adapters) {
		auto adapter = dynamic_cast<PacketStreamAdapter*>(ref.ptr);
		assert(adapter);
		traceL("PacketStreamBase", this) << "On state change: Current adapter: " << adapter << endl;
		if (adapter)
			adapter->onStreamStateChange(state);
	}
	*/
	
	// Send the StateChange signal to listeners
	//return Stateful<PacketStreamState>::setState(sender, id, message);

	
		// Wait for the Closed state to synchronize when using a 
		// thread-based Runner in order to ensure safe destruction 
		// of stream adapters.
		//if (async()) {
			//if (Thread::currentID() != _base->_runner->tid())
			//	_base->waitForStateSync(PacketStreamState::Closed);

			// We could wait for the runner to close to be completely safe,
			// but this will not work for shared Runners (in the future).
			//_base->waitForRunner();
		//}
	
	/*
	// Run close sequence if the stream is active
	if (!_base->stateEquals(PacketStreamState::None)) {

		// Stop the stream gracefully (if running)
		if (!_base->stateEquals(PacketStreamState::Stopped) &&
			!_base->stateEquals(PacketStreamState::Stopping))
			stop();

		// Queue the Closed state
		_base->setState(this, PacketStreamState::Closed);
	
		// Wait for the Closed state to synchronize when using an 
		// asynchronous Runner in order to ensure safe destruction 
		// of stream adapters.
		if (async()) {
			_base->waitForStateSync(PacketStreamState::Closed);

			// We could wait for the runner to close to be completely safe,
			// but this will not work for shared Runners (in the future).
			//_base->waitForRunner();
		}

		// Send the Closed signal
		Close.emit(this);
	}
	
	// Always detach and free managed stream adapters
	traceL("PacketStream", this) << "Close: Cleanup: " << _base << endl;
	_base->teardown();
	_base->cleanup();
	*/


/*
	
	// Dispose of the stream when Closed
	//if (state.equals(PacketStreamState::Closed))
	//	dispose();
		// Enable the incoming queue
		//RunnableQueue<IPacket>::cancel(false);

		// Start synchronized sources, except when resetting
		// Depreciate in favour of PacketStreamAdapters better 
		// handling PacketStreamState in the near future.
		//if (!oldState.equals(PacketStreamState::Resetting)) {
		//	startSources();
		//}
		//break;
		//{	
			// Disable the incoming queue
			// No more packets will be sent to the internal dispatch() method
			//RunnableQueue<IPacket>::cancel(true);

			// Stop synchronized sources
			// Depreciate in favour of PacketStreamAdapters better 
			// handling PacketStreamState in the near future.
			//stopSources();
		//}
		//break;
void PacketStreamBase::dispose()
{
	traceL("PacketStreamBase", this) << "Dispose: " << state() << endl;

	// We loose the stream pointer on PacketStream::dispose()
	assert(stateEquals(PacketStreamState::None)
		|| stateEquals(PacketStreamState::Closed));
	//assert(!_stream); // == nullptr
	assert(!_deleted);
	_deleted = true;

	// Cancel the async runner.
	// When the thread exists the onexit function will destroy
	// the stream base instance.
	//_runner->cancel();

	// Cancel the queue so the async runner context will  
	// exit on next iteration.
	//RunnableQueue<IPacket>::cancel(true);
	
	// Schedule the pointer for deferred deletion.
	//deleteLater<PacketStreamBase>(_self); //.lock()
	//if (_self) {
	//	deleteLater<PacketStreamBase>(*_self);
	//	(*_self).reset(); // = nullptr;
	//}
	//deleteLater<PacketStreamBase>(std::shared_ptr<PacketStreamBase>(this));
	//_self
	//_self = nullptr;

	traceL("PacketStreamBase", this) << "Dispose: OK: " << endl;
}
*/


	
//#include "scy/interface.h"
	//_base(std::make_shared<PacketStreamBase>(this)),
	// Dispose of the base context here if there is no active async context.
	// TODO: Use shared_ptr for auto deref and destroy when safe?
	//if (_base) {
		//_base.reset();
		//_base->_runner.reset();
		//_base = nullptr;
		//_base->waitForRunner();
		//if (!_base->_runner || 
		//	!_base->_runner->started())		
		//	_base->dispose();
	//}

/*
	//_queue.ondispatch = std::bind(&PacketStreamBase::process, this, std::placeholders::_1);

	//assert(stateEquals(PacketStreamState::Closed));
	//cleanup();
		
	// Note: The PacketStreamBase is destroyed via the async context
	// once the internal state machine is complete. See onStateChange()

			// Flush and emit all remaining queue items.
			//traceL("PacketStreamBase", this) << "Stopping: Flush" << endl;
			//RunnableQueue<IPacket>::flush();
			//assert(RunnableQueue<IPacket>::queue().empty());

			// Disable the emitter. 
			// No more outgoing packets will be sent.
			//emitter.enable(false);
	
			// Detach the processor chain.
			// Incoming packets will still be proxied to the sync()
			// method, but will bypass all processors.
			//teardown();

	// Queue the StateChange signal for synchronization 
	// with the main thread; see sync().
	//Mutex::ScopedLock lock(_mutex);
	//_syncStates.push_back(std::pair<PacketStreamState, PacketStreamState>(state, oldState));

	
void PacketStreamBase::processPendingStates()
{
	// Process queued internal states first
	while (!_states.empty()) {
		PacketStreamState::ID id;
		{
			Mutex::ScopedLock lock(_mutex);
			id = _states.front();
			_states.pop_front();
		}	

		traceL("PacketStreamBase", this) << "Set pending state: " << id << endl;	
		setState(this, id);
	}
}

		// Trigger onStateChange, but do send the StateChange signal. 
void PacketStreamBase::processSyncStates()
{		
	// Process synchronized states before emitting any packets.
	// StateChange signals are send here inside the synchroinization so 
	// the application does not need to concern itself with thread safety. 
	while (!_syncStates.empty()) {
		PacketStreamState state;
		PacketStreamState oldState;
		{
			Mutex::ScopedLock lock(_mutex);
			state = _syncStates.front().first;
			oldState = _syncStates.front().second;
			_syncStates.pop_front();
		}	
		traceL("PacketStreamBase", this) << "Set synchronized state: " << state << endl;	

		// Send the StateChange from the PacketStream instance
		if (stream())
			StateChange.emit(stream(), state, oldState);

		//Stateful<PacketStreamState>::onStateChange(state, oldState);
	}
}
*/

/*
RunnableQueue<IPacket>& PacketStreamBase::queue()
{
	Mutex::ScopedLock lock(_mutex);
	return _queue;
}
*/

/*
void PacketStreamBase::sync(IPacket& packet)
{
	traceL("PacketStreamBase", this) << "Sync: " << packet.size() << endl;
		
	// Synchronize the packet with the main thread if required.
	//if (_sync)
	//	_sync->push(packet.clone());
	//else
	emit(packet);	
}
*/

	//if (!active()) {
	//	debugL("PacketStream", this) << "Dropping packet: " << state() << endl;
	//	return;
	//}
	//push(new RawPacket(data, len));
	
	// Push the copied packet for async processing
	//if (!active()) {
	//	debugL("PacketStream", this) << "Dropping packet: " << state() << endl;
	//	return;
	//}

	// Push the nocopy packet for async processing
	//push(new RawPacket(data, len));
	
	////assert(Thread::currentID() != _thread.tid());

	// TODO: Should we throw instead? 
	// Sources would need to swallow exceptions...
	//if (!active()) {
	//	debugL("PacketStream", this) << "Dropping packet: " << state() << endl;
	//	return;
	//}
	
	////assertCmdThread();
	//assertNotActive();


/*


std::string PacketStream::name() const
{
	Mutex::ScopedLock lock(_mutex);
	return _name;
}
		//throw std::runtime_error("Only an inactive stream can be locked");
*/


/*
void PacketStream::assertCmdThread()
{
	//if (Thread::currentID() != _thread.tid()) {
	//	assert(0);
	//	throw std::runtime_error("Stream error: Cannot interact from inside stream callback.");
	//}
}
*/
/*
*/
	/*
#if 0
			{
				Mutex::ScopedLock lock(_mutex);
				auto it = _base->_processors.begin();
				if (it != _base->_processors.end()) {

					firstProc = reinterpret_cast<PacketProcessor*>((*it).ptr);
					if (!firstProc->accepts(packet)) {
						warnL("PacketStream", this) << "Source packet rejected: " 
							<< firstProc << ": " << packet.className() << endl;
						firstProc = nullptr;
					}
				}
			}
#endif


	while(_scopeRef.load() > 0) {
		traceL("PacketStream", this) << "Waiting for ready: " << _scopeRef.load() << endl;
		scy::sleep(50);
		if (times++ > 100) {
			assert(0 && "deadlock; calling inside stream scope?");
		}
	}
	*/

			/*
			if ((*sit).syncState) {
				auto startable = dynamic_cast<async::Startable*>((*sit).ptr);
				if (startable) {
					//traceL("PacketStream", this) << "Stopping startable: " << source << endl;
					startable->stop();
				}
				else assert(0 && "unknown synchronizable");
#if 0
				auto runnable = dynamic_cast<async::Runnable*>((*sit).ptr);
				if (runnable) {
					traceL("PacketStream", this) << "Stopping runnable: " << source << endl;
					runnable->cancel();
				}
				assert(startable || runnable && "unknown synchronized adapter");
#endif
			}
			*/

		
		/*
		try {	
			// Initialize synchronized sources
			// TODO: Start sources on next loop iteration, or ensure they are asynchronous
			for (auto sit = sources.begin(); sit != sources.end(); ++sit) {
				PacketStreamAdapter* source = (*sit).ptr;
				if ((*sit).syncState) {				
					auto startable = dynamic_cast<async::Startable*>(source);
					if (startable) {
						//traceL("PacketStream", this) << "Starting: " << source << endl;
						startable->start();
					}
					else assert(0 && "unknown synchronizable");
#if 0
					else {
						auto runnable = dynamic_cast<async::Runnable*>(source);
						if (runnable) {
							traceL("PacketStream", this) << "Starting runnable: " << source << endl;
							runnable->run();
						}
					}
#endif
				}
			}		
		}
		catch (std::exception& exc) {
			errorL("PacketStream", this) << "Source error: " << exc.what() << endl;
			setState(this, PacketStreamState::Error, exc.what());
		}	
		*/
	
/*
void PacketStreamAdapter::setEmitter(PacketSignal* emitter)
{
	if (_emitter) {
		assert(0 && "emitter instance cannot be changed");
		throw std::runtime_error("Stream adapter emitter instance cannot be changed.");
	}
	_emitter = emitter;
}

	if (!_emitter) {
		assert(0 && "emitter must be set");
		throw std::runtime_error("Stream adapter emitter must be set.");
	}
*/


/*
bool PacketStream::waitForRunner()
{	
	int times = 0;
	while(_scopeRef.load() > 0) {
		traceL("PacketStream", this) << "Waiting for ready: " << _scopeRef.load() << endl;
		scy::sleep(50);
		if (times++ > 100) {
			assert(0 && "deadlock; calling inside stream scope?");
		}
	}
	return true;
}
*/


/*

	//bool res = true; //_ready.tryWait(2000);
	//traceL("PacketStream", this) << "Waiting for ready: " << res << endl;
	//assert(res && "packet stream locked");
	//return res;
void PacketStream::destroy()
{
	//traceL("PacketStream", this) << "Destroy later" << endl;
	// 
	close();
	deleteLater<PacketStream>(this);
}
*/



/*
void PacketStream::write(const std::string& str)
{
	write(rawPacket(str.c_str(), str.length()));
}
*/
	

/*
void PacketStream::attach(const PacketDelegateBase& delegate)
{
	//traceL("PacketStream", this) << "Attach delegate: " << delegate.object() << endl;
	emitter.attach(delegate);
}


bool PacketStream::detach(const PacketDelegateBase& delegate)
{
	//traceL("PacketStream", this) << "Detach delegate: " << delegate.object() << endl;
	return emitter.detach(delegate);
}
*/