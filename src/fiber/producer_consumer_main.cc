#include <iostream>
#include <thread>
#include <deque>
#include <string>
#include <chrono> // For std::chrono::seconds and milliseconds

// Include Abseil headers for Mutex and Time
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

// Shared data: a queue and its associated mutex
std::deque<int> shared_queue ABSL_GUARDED_BY(queue_mu_);
absl::Mutex queue_mu_;

// Maximum size of the queue
const int kQueueCapacity = 5;

// Predicate for the consumer: queue is not empty
absl::Condition QueueNotEmpty() {
    return absl::Condition(
        +[](std::deque<int>* shared_queue) { return !shared_queue->empty(); }, &shared_queue
    );
}

// Predicate for the producer: queue is not full
absl::Condition QueueNotFull() {
    return absl::Condition(
        +[] (std::deque<int>* shared_queue ){ return shared_queue->size() < kQueueCapacity; }, &shared_queue
    );
}

// Function for the producer thread
void ProducerThread() {
    for (int i = 0; i < 10; ++i) {
        // Acquire the mutex and wait until the queue is not full
        absl::MutexLock lock(&queue_mu_);
        queue_mu_.Await(QueueNotFull());

        // Add item to the queue
        shared_queue.push_back(i);
        std::cout << "Producer: Added item " << i << ". Queue size: " << shared_queue.size() << std::endl;

        // Simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Mutex is released when 'lock' goes out of scope,
        // implicitly notifying consumers if QueueNotEmpty becomes true.
    }
    std::cout << "Producer: Finished producing." << std::endl;
}

// Function for the consumer thread
void ConsumerThread() {
    for (int i = 0; i < 10; ++i) {
        // Acquire the mutex and wait until the queue is not empty
        absl::MutexLock lock(&queue_mu_);
        queue_mu_.Await(QueueNotEmpty());

        // Remove item from the queue
        int item = shared_queue.front();
        shared_queue.pop_front();
        std::cout << "Consumer: Consumed item " << item << ". Queue size: " << shared_queue.size() << std::endl;

        // Simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        // Mutex is released when 'lock' goes out of scope,
        // implicitly notifying producers if QueueNotFull becomes true.
    }
    std::cout << "Consumer: Finished consuming." << std::endl;
}

int main() {
    std::cout << "Main: Starting producer and consumer threads." << std::endl;

    // Create the threads
    std::thread producer(ProducerThread);
    std::thread consumer(ConsumerThread);

    // Join the threads to ensure they complete before main exits
    producer.join();
    consumer.join();

    std::cout << "Main: All threads finished." << std::endl;

    return 0;
}
