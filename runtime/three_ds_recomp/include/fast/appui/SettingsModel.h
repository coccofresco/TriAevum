#pragma once
#include <functional>
#include <algorithm>
#include <exception>
#include <cmath>
#include <type_traits>
#include <string>
#include <vector>

namespace Fast::AppUi {
enum class FieldKind { Toggle, Number, Choice, Action, Text };
struct Option { std::string Value, Label; };
struct Field {
    std::string Id, Label;
    FieldKind Kind = FieldKind::Text;
    std::function<std::string()> Read;
    // Return an empty string on success, otherwise a user-facing error.
    std::function<std::string(const std::string&)> Write;
    std::vector<Option> Options;
    double Minimum = 0, Maximum = 1, Step = 1;
    std::function<bool()> Enabled;
    std::function<std::string(const std::string&)> UnavailableReason;
};
struct Page { std::string Id, Label; std::vector<Field> Fields; };
using Pages = std::vector<Page>;

template<class Config, class Value, class Read, class Write>
Field Member(std::string id, std::string label, Value Config::* member, Read read, Write write,
             double minimum=0, double maximum=1, double step=1, std::vector<Option> options={}) {
    Field field;
    field.Id=std::move(id); field.Label=std::move(label);
    field.Kind=!options.empty()?FieldKind::Choice:std::is_same_v<Value,bool>?FieldKind::Toggle:FieldKind::Number;
    field.Options=std::move(options); field.Minimum=minimum; field.Maximum=maximum; field.Step=step;
    field.Read=[read,member] {
        const auto value=read().*member;
        if constexpr(std::is_floating_point_v<Value>) {
            auto text=std::to_string(value);
            while(text.size()>1 && text.back()=='0')text.pop_back();
            if(text.back()=='.')text.pop_back();
            return text;
        }
        else return std::to_string(static_cast<int>(value));
    };
    const auto choices=field.Options;
    field.Write=[read,write,member,minimum,maximum,choices](const std::string& value)->std::string {
        try {
            size_t end=0; const double number=std::stod(value,&end);
            if(end!=value.size()||!std::isfinite(number)||number<minimum||number>maximum)
                return "Value outside the supported range";
            if constexpr(!std::is_floating_point_v<Value>)
                if(std::floor(number)!=number) return "An integer value is required";
            if(!choices.empty() && std::none_of(choices.begin(),choices.end(),[&](const auto& o){return o.Value==value;}))
                return "Unsupported option";
            auto config=read(); config.*member=static_cast<Value>(number); return write(config);
        } catch(const std::exception&) {return "Invalid numeric value";}
    };
    return field;
}
}
