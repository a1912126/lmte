#pragma once

#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

enum class ValueType { Int, Float, String, Bool, Void, Object, Enum, List, Any };

std::string typeName(ValueType type);

struct ObjectInstance;

struct Value {
  ValueType type = ValueType::Void;
  long long i = 0;
  double f = 0.0;
  std::string s;
  bool b = false;
  std::string enumType;
  std::string enumMember;
  long long enumIndex = 0;
  std::shared_ptr<ObjectInstance> object;
  std::shared_ptr<std::vector<Value>> list;

  static Value makeInt(long long x);
  static Value makeFloat(double x);
  static Value makeString(const std::string &x);
  static Value makeBool(bool x);
  static Value makeObject(std::shared_ptr<ObjectInstance> x);
  static Value makeEnum(const std::string &type, const std::string &member, long long index);
  static Value makeList();
  static Value makeList(std::vector<Value> values);
  std::string toString() const;
};

struct ObjectInstance {
  std::string className;
  std::unordered_map<std::string, Value> fields;
};
