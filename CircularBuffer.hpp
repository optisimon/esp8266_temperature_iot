#pragma once

template<class T, int N>
class CircularBuffer {
public:
  CircularBuffer() : _readPos(0), _writePos(0), _size(0) {
    
  }
  ~CircularBuffer() {
    // NO CODE
  }
  void fill(T value) // TODO: consider using ref (and overload it for rvalues)
  {
    _size = N;
    _readPos = 0;
    _writePos = 0;
    for (int i = 0; i < N; i++)
    {
      _data[i] = value;
    }
  }
  /** @return false if no more space available */
  bool push_back(const T& value) {
     if (_size == N) {
       return false;
     }
     _data[_writePos] = value;
     _writePos = getNextPos(_writePos);
     _size++;
  }
  void pop_front() {
    if (_size < 1)
    {
      _size = 0;
      return; // decide what to do in this error case
    }
    else
    {
      _readPos = getNextPos(_readPos);
      _size--;
    }
  }
  bool push_back_erase_if_full(const T& value) {
     while (_size >= N) {
       pop_front();
     }
     _data[_writePos] = value;
     _writePos = getNextPos(_writePos);
     _size++;
  }
  int size() const { return _size; }
  
  T& operator[](int pos)
  {
    if (pos > _size)
    {
      static T nullInitialized{};
      Serial.println("ERROR: pos > size in circular buffer");
      return nullInitialized; // todo decide what to do in this error case...
    }
    else
    {
      int offset = _readPos + pos;
      if (offset >= N)
        offset -= N;
      return _data[offset];
    }
  }
private:
  int getNextPos(int currentPos)
  {
    if ((currentPos + 1) < N)
      return currentPos + 1;
    else
      return 0;
  }
  T _data[N];
  int _readPos;
  int _writePos;
  int _size;
};
