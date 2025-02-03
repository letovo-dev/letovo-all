#pragma once

#include <memory>
#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include "../market/actives.h"
#include <mutex>
#include <condition_variable>


namespace actives::deals {
    struct bid {
        int price;
        int bid_id;
        std::string owner;
        actives::active_obj active;
        int amount;
        bool resolved = false;

        bid(int p, int b_id, const std::string& o, actives::active_obj act, int am, bool r = false) : price(p), bid_id(b_id), owner(o), active(act), amount(am), resolved(r) {}

        bid() {};
    };

    std::string serialaze(const std::vector<bid>&);
}

namespace utils {
    class LinkedListNode {
        public:
            int val;
            int number;
            actives::deals::bid bidVal;
            std::string stringVal;
            LinkedListNode(std::shared_ptr<LinkedListNode> n = nullptr);
            LinkedListNode(int, std::shared_ptr<LinkedListNode> n = nullptr);
            LinkedListNode(std::string, std::shared_ptr<LinkedListNode> n = nullptr);
            LinkedListNode(actives::deals::bid, std::shared_ptr<LinkedListNode> n = nullptr);
            
        private:
            std::shared_ptr<LinkedListNode> child;
            std::shared_ptr<LinkedListNode> parent;

        friend class LinkedList;
    };

    class LinkedList {
        private:
            std::shared_ptr<LinkedListNode> first;
            std::shared_ptr<LinkedListNode> last;
            bool busy = false;
            std::mutex mtx;
            std::condition_variable cv;
        public:
            void pop();

            void push(int value);
            void push(const std::string& value);
            void push(actives::deals::bid value);
            void push(const std::shared_ptr<LinkedListNode>& node);

            std::shared_ptr<LinkedListNode> top();

            actives::deals::bid delBid(int bid_id);

            actives::deals::bid findBid(int bid_id);

            std::vector<actives::deals::bid> byUser(std::string user_name);

            bool empty();

            LinkedList(std::vector<actives::deals::bid>);
            LinkedList(std::vector<std::string>);
            LinkedList(std::vector<int>);
            LinkedList();
    };

}