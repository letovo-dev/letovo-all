#include "utils.h"

namespace utils {
    LinkedListNode::LinkedListNode(std::shared_ptr<LinkedListNode> n) {
        val = 0;
        parent = n;
        stringVal = "";
        number = 0;
    }

    LinkedListNode::LinkedListNode(int x, std::shared_ptr<LinkedListNode> n) {
        val = x;
        parent = n;
        stringVal = std::to_string(x);
        number = x;
    }

    LinkedListNode::LinkedListNode(std::string x, std::shared_ptr<LinkedListNode> n) {
        val = 0;
        parent = n;
        stringVal = x;
        number = 0;
    }

    LinkedListNode::LinkedListNode(actives::deals::bid x, std::shared_ptr<LinkedListNode> n) {
        val = 0;
        parent = n;
        bidVal = x;
        number = 0;
    }

    LinkedList::LinkedList() {
        this->first = nullptr;
        this->last = nullptr;
    }

    LinkedList::LinkedList(std::vector<std::string> v) {
        if(v.size() == 0) {
            throw std::invalid_argument("Vector is empty");
        }
        std::shared_ptr<LinkedListNode> head = std::make_shared<LinkedListNode>(v[0]);
        std::shared_ptr<LinkedListNode> current = head;

        for (int i = 1; i < v.size(); i++) {
            current->child = std::make_shared<LinkedListNode>(v[i], current);
            current = current->child;
        }
        this->first = head;
        this->last = current;
    }

    LinkedList::LinkedList(std::vector<int> v) {
        if(v.size() == 0) {
            throw std::invalid_argument("Vector is empty");
        }
        std::shared_ptr<LinkedListNode> head = std::make_shared<LinkedListNode>(v[0]);
        std::shared_ptr<LinkedListNode> current = head;

        for (int i = 1; i < v.size(); i++) {
            current->child = std::make_shared<LinkedListNode>(v[i], current);
            current = current->child;
        }
        this->first = head;
        this->last = current;
    }

    void LinkedList::pop() {
        if(this->first == nullptr) {
            return;
        }
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !busy; });
        busy = true;

        if(this->last == this->first) {
            this->first = nullptr;
            this->last = nullptr;
            busy = false;
            lock.unlock();
            cv.notify_one();
            return;
        }
        this->last->parent->child = nullptr;
        this->last = this->last->parent;
        busy = false;
        lock.unlock();
        cv.notify_one();
    }

    void LinkedList::push(int value) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !busy; });
        busy = true;
        std::shared_ptr<LinkedListNode> newNode = std::make_shared<LinkedListNode>(value);
        if(this->first == nullptr) {
            this->first = newNode;
            this->last = newNode;
            return;
        }
        newNode->child = this->first->child;
        this->first->parent = newNode;
        this->first = newNode;
        busy = false;
        lock.unlock();
        cv.notify_one();
    }

    void LinkedList::push(const std::string& value) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !busy; });
        busy = true;
        std::shared_ptr<LinkedListNode> newNode = std::make_shared<LinkedListNode>(value);
        if(this->first == nullptr) {
            this->first = newNode;
            this->last = newNode;
            busy = false;
            lock.unlock();
            cv.notify_one();
            return;
        }
        newNode->child = this->first->child;
        this->first->parent = newNode;
        this->first = newNode;
        busy = false;
        lock.unlock();
        cv.notify_one();
    }

    void LinkedList::push(actives::deals::bid value) {
        std::shared_ptr<LinkedListNode> newNode = std::make_shared<LinkedListNode>(value);
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !busy; });
        busy = true;
        if(this->first == nullptr) {
            this->first = newNode;
            this->last = newNode;
            return;
        }
        newNode->child = this->first->child;
        this->first->parent = newNode;
        this->first = newNode;
        busy = false;
        lock.unlock();
        cv.notify_one();
    }

    std::shared_ptr<LinkedListNode> LinkedList::top() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !busy; });
        busy = true;
        if (this -> empty()) {
            busy = false;
            lock.unlock();
            cv.notify_one();
            return nullptr;
        }
        auto l =  this->last;
        busy = false;
        lock.unlock();
        cv.notify_one();
        return  l;
    }
    
    void LinkedList::delBid(int bid_id) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !busy; });
        busy = true;
        std::shared_ptr<LinkedListNode> current = this->first;
        while(current != nullptr) {
            if(current->bidVal.bid_id == bid_id) {
                if(current->parent != nullptr) {
                    current->parent->child = current->child;
                }
                if(current->child != nullptr) {
                    current->child->parent = current->parent;
                }
                return;
            }
            current = current->child;
        }
        busy = false;
        lock.unlock();
        cv.notify_one();
    }

    bool LinkedList::empty() {
        return this->first == nullptr;
    }
}