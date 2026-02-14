# **mruby‑fast‑json**  
A high‑performance JSON parser and encoder for MRuby, powered by **simdjson**.

`mruby-fast-json` provides:

- **Ultra‑fast JSON.parse** using simdjson’s DOM parser  
- **Strict error reporting** mapped to Ruby exception classes  
- **Full UTF‑8 validation** on both parse and dump  
- **Optional symbolized keys** (`symbolize_names: true`)  
- **JSON.dump** with correct escaping and Unicode handling  
- **Round‑trip safety** for all supported types  
- **Big integer support** (uint64 → MRuby integer)  
- **Precise error classes** for malformed JSON

This gem is designed to be a drop‑in replacement for `JSON.parse` and `JSON.dump` in MRuby environments where performance and correctness matter.

---

## **Features**

### ✔ Fast, SIMD‑accelerated parsing  
Backed by simdjson, parsing is extremely fast even for large documents.

### ✔ Symbolized keys  
```ruby
JSON.parse('{"name":"Alice"}', symbolize_names: true)
# => { :name => "Alice" }
```

### ✔ Full UTF‑8 validation  
Invalid UTF‑8 sequences raise `JSON::UTF8Error`.

### ✔ Correct JSON escaping  
All control characters, quotes, backslashes, and C0 controls are escaped according to the JSON spec.

### ✔ Big integer support  
Numbers larger than `INT64_MAX` become MRuby integers, not floats.

### ✔ Detailed error classes  
Malformed JSON raises specific exceptions such as:

- `JSON::TapeError`
- `JSON::StringError`
- `JSON::UnclosedStringError`
- `JSON::DepthError`
- `JSON::NumberError`
- `JSON::BigIntError`
- `JSON::UnescapedCharsError`
- …and many more

---

## **Usage**

### **Parsing JSON**

```ruby
obj = JSON.parse('{"name":"Alice","age":30}')
obj["name"]  # => "Alice"
obj["age"]   # => 30
```

### **Symbolized keys**

```ruby
obj = JSON.parse('{"name":"Alice"}', symbolize_names: true)
obj[:name]   # => "Alice"
obj["name"]  # => nil
```

### **Nested structures**

```ruby
obj = JSON.parse('{"user":{"id":1,"name":"Bob"}}')
obj["user"]  # => { "id" => 1, "name" => "Bob" }
```

### **Arrays**

```ruby
arr = JSON.parse('[true, null, 42, "hi"]')
# => [true, nil, 42, "hi"]
```

---

## **Dumping JSON**

```ruby
JSON.dump({ "x" => 1, "y" => "z" })
# => '{"x":1,"y":"z"}'
```

### **Arrays**

```ruby
JSON.dump([true, nil, "text"])
# => '[true,null,"text"]'
```

### **UTF‑8 round‑trip**

```ruby
obj = { "emoji" => "😀😃😄" }
json = JSON.dump(obj)
JSON.parse(json)  # => same structure
```

---

## **Error Handling**

Malformed JSON raises specific exceptions:

```ruby
JSON.parse('{"a":1,}')        # => JSON::ParserError
JSON.parse('"unterminated')   # => JSON::UnclosedStringError
JSON.parse('tru')             # => JSON::TAtomError
JSON.parse('"\xC0"')          # => JSON::StringError
JSON.parse('{"x":12.3.4}')    # => JSON::NumberError
JSON.parse('')                # => JSON::EmptyInputError
```

Invalid UTF‑8 inside strings:

```ruby
JSON.parse("\"\xC0\xAF\"")
# => JSON::UTF8Error
```

Huge integers:

```ruby
JSON.parse('{"x":' + '9' * 20000 + '}')
# => JSON::BigIntError
```

---

## **Escaping Rules**

`JSON.dump` escapes strings according to the JSON spec:

- Printable ASCII → unchanged  
- Quotes and backslashes → escaped  
- Control chars → `\b \f \n \r \t`  
- Other C0 controls → `\u00XX`  
- Valid UTF‑8 → preserved  

Example:

```ruby
JSON.dump("\"\bλ😀\n")
# => "\"\\\"\\bλ😀\\n\""
```

---

## **Development & Testing**

The test suite covers:

- Parsing primitives  
- Symbolized keys  
- Nested structures  
- UTF‑8 correctness  
- Error conditions  
- Escaping rules  
- Big integer handling  
- Round‑trip stability  

Run tests with:

```
rake test
```

---

## **License**

MIT — same as MRuby and simdjson.
