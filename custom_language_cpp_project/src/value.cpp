#include "value.hpp"

std::string typeName(ValueType t) {
    switch (t) {
        case ValueType::Int: return "int";
        case ValueType::Float: return "float";
        case ValueType::String: return "string";
        case ValueType::Bool: return "bool";
        case ValueType::Void: return "void";
        case ValueType::Object: return "object";
        case ValueType::Enum: return "enum";
        case ValueType::List: return "list";
        case ValueType::Any: return "any";
    }
    return "unknown";
}

Value Value::makeInt(long long x) { Value v; v.type = ValueType::Int; v.i = x; return v; }
Value Value::makeFloat(double x) { Value v; v.type = ValueType::Float; v.f = x; return v; }
Value Value::makeString(const std::string& x) { Value v; v.type = ValueType::String; v.s = x; return v; }
Value Value::makeBool(bool x) { Value v; v.type = ValueType::Bool; v.b = x; return v; }
Value Value::makeObject(std::shared_ptr<ObjectInstance> x) {
    Value v;
    v.type = ValueType::Object;
    v.object = std::move(x);
    return v;
}
Value Value::makeEnum(const std::string& type, const std::string& member, long long index) {
    Value v;
    v.type = ValueType::Enum;
    v.enumType = type;
    v.enumMember = member;
    v.enumIndex = index;
    return v;
}
Value Value::makeList() { return makeList({}); }
Value Value::makeList(std::vector<Value> values) {
    Value v;
    v.type = ValueType::List;
    v.list = std::make_shared<std::vector<Value>>(std::move(values));
    return v;
}

std::string Value::toString() const {
    switch (type) {
        case ValueType::Int: return std::to_string(i);
        case ValueType::Float: {
            std::ostringstream os;
            os << std::setprecision(12) << f;
            return os.str();
        }
        case ValueType::String: return s;
        case ValueType::Bool: return b ? "true" : "false";
        case ValueType::Void: return "";
        case ValueType::Object: return object ? "<" + object->className + ">" : "<null>";
        case ValueType::Enum: return std::to_string(enumIndex);
        case ValueType::List: return list ? "<list:" + std::to_string(list->size()) + ">" : "<list>";
        case ValueType::Any: return "<any>";
    }
    return "";
}
