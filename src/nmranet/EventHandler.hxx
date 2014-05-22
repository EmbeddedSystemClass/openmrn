/** \copyright
 * Copyright (c) 2013, Balazs Racz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 * 
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file NMRAnetEventRegistry.hxx
 * Interface for handling NMRAnet events.
 *
 * @author Balazs Racz
 * @date 29 September 2013
 */

#ifndef _NMRANET_EVENTHANDLER_HXX_
#define _NMRANET_EVENTHANDLER_HXX_

#include <stdint.h>

#include "executor/Notifiable.hxx"
#include "utils/AsyncMutex.hxx"
#include "utils/macros.h"
#include "nmranet/WriteHelper.hxx"

namespace nmranet
{

typedef uint64_t EventId;
class Node;

enum EventState {
  VALID = 0,
  INVALID = 1,
  UNKNOWN = 2,
  RESERVED = 3
};

/*enum EventMask {
  EVENT_EXACT_MASK = 1,
  EVENT_ALL_MASK = 0xffffffffffffffffULL
  };*/

typedef struct {
  EventId event;
  EventId mask;
  NodeHandle src_node;
  Node* dst_node;
  EventState state;
} EventReport;

// Static objects usable by all event handler implementations

// These allow event handlers to produce up to four messages per
// invocation. They are locked by the event-handler_mutex and always available
// at the entry to an event handler function.
extern WriteHelper event_write_helper1;
extern WriteHelper event_write_helper2;
extern WriteHelper event_write_helper3;
extern WriteHelper event_write_helper4;

class EventHandler {
public:
  virtual ~EventHandler() {}

  // Called on incoming EventReport messages. Filled: src_node, event. Mask is
  // always 1 (filled in). state is not filled in.
  virtual void HandleEventReport(EventReport* event,
                                 BarrierNotifiable* done) = 0;

  // Called on another node sending ConsumerIdentified for this event. Filled:
  // event_id, mask=1, src_node, state.
  virtual void HandleConsumerIdentified(EventReport* event,
                                        BarrierNotifiable* done) {
    done->notify();
  };

  // Called on another node sending ConsumerRangeIdentified. Filled: event id, mask (!= 1), src_node. Not filled: state.
  virtual void HandleConsumerRangeIdentified(EventReport* event,
                                             BarrierNotifiable* done) {
    done->notify();
  }

  // Called on another node sending ProducerIdentified for this event. Filled: event_id, mask=1, src_node, state.
  virtual void HandleProducerIdentified(EventReport* event,
                                        BarrierNotifiable* done) {
    done->notify();
  }

  // Called on another node sending ProducerRangeIdentified for this event. Filled: event id, mask (!= 1), src_node. Not filled: state.
  virtual void HandleProducerRangeIdentified(EventReport* event,
                                             BarrierNotifiable* done) {
    done->notify();
  }

  // Called on the need of sending out identification messages. event is
  // NULL. This happens on startup, or when a global or addressed
  // IdentifyGlobal message arrives. Might have destination node id!
  virtual void HandleIdentifyGlobal(EventReport* event, BarrierNotifiable* done) = 0;

  // Called on another node sending IdentifyConsumer. Filled: src_node, event, mask=1. Not filled: state.
  virtual void HandleIdentifyConsumer(EventReport* event,
                                      BarrierNotifiable* done) = 0;

  // Called on another node sending IdentifyProducer. Filled: src_node, event, mask=1. Not filled: state.
  virtual void HandleIdentifyProducer(EventReport* event,
                                      BarrierNotifiable* done) = 0;
};

typedef void (EventHandler::*EventHandlerFunction)(EventReport* event,
                                                          BarrierNotifiable* done);

class EventIterator;

class EventRegistry {
public:
    virtual ~EventRegistry();

    static EventRegistry* instance() {
        HASSERT(instance_);
        return instance_;
    }

    /** Computes the alignment mask for registering an event range. Updates the
     * event by rounding and returns the mask value to be sent to the
     * register_handler function.
     * @param event is the event id to be registered. Will be modified.
     * @param is the number of events to register from that offset. [event,
     * event+size) will be the registration range.
     */
    static unsigned align_mask(EventId* event, unsigned size);

    /** mask = 0 means exact event only. Mask = 63 means this is a global
     * handler. Mask == 0 means we should be registered for one single
     * eventid. */
  virtual void register_handlerr(EventHandler* handler, EventId event, unsigned mask) = 0;
  virtual void unregister_handlerr(EventHandler* handler, EventId event, unsigned mask) = 0;
  
    // Creates a new event iterator. Caller takes ownership of object.
    virtual EventIterator* create_iterator() = 0;

protected:
  EventRegistry();

private:
  static EventRegistry* instance_;

  DISALLOW_COPY_AND_ASSIGN(EventRegistry);
};

}; /* namespace nmranet */

#endif  // _NMRANET_EVENTHANDLER_HXX_