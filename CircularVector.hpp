 #pragma once

template<class T>
class CircularVector
{
    public:
        CircularVector(uint8_t c_capacity) : capacity(c_capacity) {
            data = new T[capacity];
        }

        ~CircularVector() {
            delete[] data;
        }

        void push(const T &t) {
            if (sz < capacity) {
                data[sz] = t;
                ++sz;
            }
            else {
                for (uint8_t i = 1; i < sz; ++i)
                    data[i - 1] = data[i];
                data[capacity - 1] = t;
            }
        }

        T& operator[](uint8_t i) {
            return data[i];
        }

        uint8_t size() {
            return sz;
        }

        uint8_t maxSize() {
            return capacity;
        }

    private:
        T *data;
        uint8_t sz = 0;
        const uint8_t capacity;
};

class BoolCircularVector
{
    public:
        BoolCircularVector(uint8_t c_capacity) : capacity(c_capacity) {
            data = new uint8_t[(int) ceil(static_cast<float>(c_capacity) / 8)];
        }

        ~BoolCircularVector() {
            delete[] data;
        }

        bool get(uint8_t i) const {
            return (data[i / 8] >> (i % 8)) & 1;
        }

        void push(bool val) {
            if (sz < capacity) {
                set(sz, val);
                ++sz;
            }
            else {
                for (uint8_t i = 1; i < sz; ++i)
                    set(i - 1, get(i));
                set(capacity - 1, val);
            }
        }

        void set(uint8_t i, bool val) {
            if (val)
                data[i / 8] |= 1 << (i % 8);
            else
                data[i / 8] &= ~(1 << (i % 8));
        }

        uint8_t size() const {
            return sz;
        }

        uint8_t maxSize() {
            return capacity;
        }

        uint8_t *data;
        uint8_t sz = 0;
        const uint8_t capacity;
};
