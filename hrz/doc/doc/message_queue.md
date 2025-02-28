---
Title: The message queue
Category: General
---

Until now, we have only seen how the client can communicate with Horizon. However sometimes it is the Core backend that wishes to communicate with the client. This is done through a system called the message queue. This system must be used similarily to the event queue of a windowing system: it must be polled by the client.

The message queue is accessed through the [message queue service](HrzProtocol.MessageQueueService.html). It is polled by querying the `DequeueMessages` method. It returns both the array of returned messages and the number of messages remaining in the queue if they were not all queried. Note that once a message has been queried, it is removed from the queue. This means that if a message requires an action, it should be done by whomever dequeued the message, and not discarded in hope that another system will handle it. It is safe to poll for messages often, up to once per frame, though being more conservative is recommended as all messages go through serialization.

Each [message](HrzProtocol.TypedMessage.html) has a `type` field, which is an enumeration that tells the client what type of message has been received. The actual payload is contained in one of the other field on the message, depending on its type.

See the documentation of each message type to see what action should be taken upon receiving them.

### Example in TypeScript

```ts
let messagePumpInterval: ReturnType<typeof setInterval>;

function startMessagePump() {
    messagePumpInterval = setInterval(getMessages, 30);
}

function stopMessagePump() {
    clearInterval(messagePumpInterval);
}

async function getMessages() {
    while (true) {
        const dequeuedMessages = await API.MessageQueueService.dequeueMessages({ maxMessageCount: 100 });
        const queueSize = dequeuedMessages.queueSize.messageCount;

        if (dequeuedMessages.messages.length > 0) {
            console.log("Got " + dequeuedMessages.messages.length + " messages.");
        }

        for (let i = 0; i < dequeuedMessages.messages.length; i++)
        {
            const message = dequeuedMessages.messages[i];
            console.log("Got message of type " + message.type);

            // Do something with the message...
        }

        if (queueSize === 0) {
            break;
        }

        console.log(queueSize + " messages left in the queue.");
    }
}
```
