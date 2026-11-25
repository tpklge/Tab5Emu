#pragma once
#include <memory>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <glm/glm.hpp>
#define DAEDALUS_USE(...) ((void)0)
template<class T>T make_uncached_ptr(T p){return p;}
