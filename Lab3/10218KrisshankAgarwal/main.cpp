#include<bits/stdc++.h> 
using namespace std ; 
template<typename T > 
struct Node 
{ 
    T value ; 
    Node<T >* next ; 
    Node<T >* prev ; 
    string key ; 
    int useCount ; 
} ; 

template <typename T > 
struct ConcurrentCache 
{ 
    mutex mx ; 
    map<string, Node<T >* > cacheMap ; 
    Node<T >* head ; 
    Node<T >* tail ; 
} ; 

template <typename T > 
class ClockSweep 
{ 
    private: 
        int maxSize ; 
        atomic<int > currentSize ; 
        ConcurrentCache<T >* cacheSafe ; 
        atomic<int > counter ; 

    public: 
        ClockSweep(int maxCache ) 
        { 
            maxSize= maxCache ; 
            currentSize.store(0 ) ; 
            cacheSafe->head= nullptr ; 
            cacheSafe->tail= nullptr ; 
            counter.store(0 ) ; 
        } 

        T getKey(string key ) 
        { 
            try 
            { 
                lock_guard<mutex > lock(cacheSafe->mx ) ; 
                /* cout << cacheMap.count(key ) << "H" << endl ; */ 
                if(cacheSafe->cacheMap.count(key )!= 0 ) 
                { 
                    (*(cacheSafe->cacheMap[key ] ) ).useCount++ ; 
                    pushFront(key ) ; 
                    return (*(cacheSafe->cacheMap[key ] ) ).value ; 
                } 
                throw runtime_error("Key not present here " ) ; 
            } catch (runtime_error &error ) 
            { 
                cout << error.what() << endl ; 
                return T{} ; 
            } 
        } 

        void pushFront(string key ) 
        { 
            Node<T > node= (*(cacheSafe->cacheMap[key ] ) ) ; 
            Node<T > pre= (*(node.prev ) ) ; 
            node.prev->next= node.next ; 
            node.next->prev= (&pre ) ; 
            node.next= cacheSafe->head ; 
            node.prev= nullptr ; 
        } 

        string addValue(T value ) 
        { 
            lock_guard<mutex > lock(cacheSafe->mx ) ; 
            string key ; 
            for(int i= 0 ; i< 1 ; ++i ) 
            { 
                if(currentSize< maxSize ) 
                { 
                    /* auto dur= chrono::system_clock::now().time_since_epoch() ; 
                    auto timestamp= chrono::duration_cast<chrono::milliseconds >(dur ).count() ; */ 
                    key= to_string(counter ) ; 
                    counter++ ; 
                    Node<T >* node= new Node<T >() ; 
                    (*node ).value= value ; 
                    (*node ).next= cacheSafe->head ; 
                    (*node ).prev= nullptr ; 
                    (*node ).key= key ; 
                    (*node ).useCount= 1 ; 
                    if(currentSize> 0 ) 
                    { 
                        cacheSafe->head->prev= node ; 
                    } 
                    else 
                    { 
                        cacheSafe->tail= node ; 
                    } 
                    cacheSafe->head= node ; 
                    cacheSafe->cacheMap[key ]= node ; 
                    currentSize++ ; 
                } 
                else 
                { 
                    removeKey() ; 
                    currentSize-- ; 
                    i-- ; 
                    /* string key= addValue(value ) ; 
                    return key ; */ 
                } 
            } 
            return key ; 
        } 

        void removeKey() 
        { 
            if(currentSize== 1 ) 
            { 
                cacheSafe->cacheMap.erase((*(cacheSafe->tail ) ).key ) ; 
                cacheSafe->head= nullptr ; 
                cacheSafe->tail= nullptr ; 
            } 
            else if(currentSize> 1 ) 
            { 
                cacheSafe->cacheMap.erase((*(cacheSafe->tail ) ).key ) ; 
                Node<T > nd= *((*(cacheSafe->tail ) ).prev ) ; 
                cacheSafe->tail->prev= nullptr ; 
                nd.next= nullptr ; 
                cacheSafe->tail= (&nd ) ; 
            } 
        } 

        void checkKeys() 
        { 
            lock_guard<mutex > lock(cacheSafe->mx ) ; 
            Node<T >* temp= cacheSafe->head ; 
            while(temp!= nullptr ) 
            { 
                /* cout << temp << endl ; */ 
                /* unique_lock<mutex > lock(temp->mx ) ; 
                vec.push_back(lock ) ; */ 
                temp->useCount= temp->useCount- 1 ; 
                if(temp->useCount== 0 ) 
                { 
                    cacheSafe->cacheMap.erase(temp->key ) ; 
                    if(temp== cacheSafe->head ) 
                    { 
                        cacheSafe->head= cacheSafe->head->next ; 
                        temp= cacheSafe->head ; 
                        if(cacheSafe->head!= nullptr ) 
                        { 
                            cacheSafe->head->prev= nullptr ; 
                        } 
                    } 
                    else if(temp== cacheSafe->tail ) 
                    { 
                        cacheSafe->tail= cacheSafe->tail->prev ; 
                        temp= cacheSafe->tail->next ; 
                        if(cacheSafe->tail!= nullptr ) 
                        { 
                            cacheSafe->tail->next= nullptr ; 
                        } 
                    } 
                    else 
                    { 
                        Node<T > *nd= temp->prev ; 
                        temp->prev->next= temp->next ; 
                        temp->next->prev= nd ; 
                        temp->next= nullptr ; 
                        temp->prev= nullptr ; 
                        temp= nd->next ; 
                    } 
                    currentSize-- ; 
                } 
            } 
            temp= cacheSafe->head ; 
            cout << "Cache Memory Computed After Clock Sweep Is: " << endl ; 
            while(temp!= nullptr ) 
            { 
                cout << "Key: " << temp->key << " " << "Value: " << temp->value << " " ; 
                temp= temp->next ; 
            } 
            /* int sz= vec.size() ; 
            for(int i= 0 ; i< sz ; ++i ) 
            { 
                vec[i].unlock() ; 
            } */ 
            cout << endl ; 
            cout << "Cache Memory Is Computed " << endl ; 
        } 
} ; 

template<typename T > 
void executeTasks(ClockSweep<T > &clock ) 
{ 
    vector<string > vec(8 ) ; 
    for(int i= 0 ; i<= 7 ; ++i ) 
    { 
        vec[i]= clock.addValue(i ) ; 
    } 
    cout << clock.getKey(vec[2] ) << endl ; 
    cout << clock.getKey(vec[4] ) << endl ; 
} 

template<typename T > 
void evictValue(ClockSweep<T > clock ) 
{ 
    while(true ) 
    { 
        this_thread::sleep_for(chrono::seconds(1 ) ) ; 
        clock.checkKeys() ; 
    } 
} 

int main() 
{ 
    ClockSweep<int > clock(6 ) ; 
    thread t1(executeTasks<int >, ref(clock ) ) ; 
    thread t2(evictValue<int >, ref(clock ) ) ; 

    t1.join() ; 
    t2.join() ; 
} 