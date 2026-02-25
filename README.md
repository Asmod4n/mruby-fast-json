# **mruby‑fast‑json**
A high‑performance JSON parser and encoder for MRuby, powered by Powered by [simdjson](https://github.com/simdjson/simdjson)
Learn more at https://simdjson.org


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

# **OnDemand JSON API (Lazy Parsing)**
A high‑performance, zero‑copy, streaming JSON interface for MRuby, powered by **simdjson’s OnDemand parser**.

The OnDemand API provides:

- **Lazy parsing** — fields are parsed only when accessed
- **Zero‑copy string access** when possible
- **Fast field lookup** (`doc["key"]`, `doc.at(index)`)
- **JSON Pointer support** (`doc.at_pointer("/a/b/0")`)
- **Streaming iteration** over arrays and objects
- **Deterministic error handling** mapped to Ruby exceptions
- **Native deserialization into Ruby objects** via `native_ext_deserialize`

This API is ideal for large JSON documents, streaming workloads, or performance‑critical environments.

---

# **Quick Start**

```ruby
json = '{"user":{"id":1,"name":"Alice"},"tags":[1,2,3]}'
doc  = JSON.parse_lazy(json)

doc["user"]["name"]   # => "Alice"
doc["tags"][1]        # => 2
```

Unlike `JSON.parse`, this does **not** build a full Ruby object tree.
Values are parsed on demand, directly from the underlying buffer.

---

# **Zero‑Copy Parsing**

If the input string has enough capacity for simdjson’s padding, the parser uses it directly:

```ruby
JSON.zero_copy_parsing = true
doc = JSON.parse_lazy(str)
```

If not, the string is resized and frozen, or a padded buffer is allocated.

---

# **JSON::Document API**

A `JSON::Document` represents a lazily parsed JSON value.
It supports:

## **Field Lookup**

### String keys

```ruby
doc["name"]        # => value or nil
doc.fetch("name")  # => value or raises KeyError
```

### Array indexing

```ruby
doc.at(0)          # => value or nil
doc.fetch(0)       # => value or raises IndexError
```
You may only use .at once per array, when you need to iterate over an array take a look at the Iteration APIs below.

### JSON Pointer

```ruby
doc.at_pointer("/user/name")   # => "Alice"
```

### JSON Path (simdjson extension)

```ruby
doc.at_path("$.user.id")       # => 1
```

### Wildcards

```ruby
doc.at_path_with_wildcard("$.items[*].id")
# => [1, 2, 3]
```

Or with a block:

```ruby
doc.at_path_with_wildcard("$.items[*].id") do |id|
  puts id
end
```

---

# **Iteration**

### Arrays

```ruby
doc.array_each do |value|
  puts value
end
```

Or return an array:

```ruby
doc.array_each
# => [ ... ]
```

### Objects

```ruby
doc.object_each do |key, value|
  puts "#{key} = #{value}"
end
```

---

# **Error Handling**

All simdjson errors are mapped to Ruby exceptions:

- `JSON::NoSuchFieldError`
- `JSON::OutOfBoundsError`
- `JSON::TapeError`
- `JSON::DepthError`
- `JSON::UTF8Error`
- `JSON::NumberError`
- `JSON::BigIntError`
- `JSON::UnescapedCharsError`
- `JSON::ParserInUseError`
- …and many more

Lookup misses (`NO_SUCH_FIELD`, `INDEX_OUT_OF_BOUNDS`, etc.) return **nil** for:

- `doc["key"]`
- `doc.find_field`
- `doc.find_field_unordered`
- `doc.at`
- `doc.at_pointer`
- `doc.at_path`

But raise for:

- `doc.fetch`

---

# **Native Deserialization (Zero‑Magic, Explicit Contracts)**

You can define a Ruby class with a schema:

```ruby
class Foo
  attr_accessor :foo
  native_ext_deserialize :@foo, JSON::Type::String
end
```

Then deserialize directly from an OnDemand document:

```ruby
doc = JSON.parse_lazy('{"foo":"hello"}')
foo = doc.into(Foo.new)

foo.foo   # => "hello"
```

## **How it works**

- Each class stores a hidden schema hash:
  `:@ivar => JSON::Type::X`
- The C++ layer iterates the schema and attempts to:
  - find the field
  - check the JSON type
  - convert the value
  - assign the ivar
- No fallback, no coercion, no guessing
- If at least one field matches → success
- If none match → `JSON::IncorrectTypeError`
- If simdjson reports an error → raised immediately

This is a **deterministic, explicit, zero‑magic** deserialization pipeline.

Supported Types
- JSON::Type::Array
- JSON::Type::Object
- JSON::Type::Number
- JSON::Type::String
- JSON::Type::Boolean
- JSON::Type::Null

---

# **Performance Notes**

- OnDemand parsing is **streaming**: fields are parsed only when accessed.
- You have to access fields in order or an error is thrown, when you need to start from the beginning of a stream you can call .rewind on a JSON::Document.

---

# **When to Use OnDemand**

Use OnDemand when:

- You parse large JSON documents
- You only need a subset of fields
- You want maximum performance
- You want deterministic, schema‑driven deserialization
- You want to avoid building full Ruby objects

Use DOM (`JSON.parse`) when:

- You need a complete Ruby object tree
- You want to modify the parsed structure
- You prefer simplicity over performance

---

# **Example: High‑Performance Pipeline**

```ruby
class User
  attr_accessor :id, :name

  native_ext_deserialize :@id,   JSON::Type::Number
  native_ext_deserialize :@name, JSON::Type::String
end

doc = JSON.load_lazy("users.json")

users = []
doc.array_each do |user_doc|
  u = User.new
  users << user_doc.into(u)
end
```

This avoids building any intermediate Ruby hashes or arrays.

---

# **Lazy File Loading (`JSON.load_lazy`)**

`JSON.load_lazy` loads a JSON file into a **padded_string** and returns a lazily‑parsed `JSON::Document`.
This is the most efficient way to process large JSON files in MRuby.

Unlike `JSON.parse(File.read(...))`, this API:

- avoids allocating a Ruby string for the entire file
- uses simdjson’s **padded_string::load** for optimal I/O
- parses lazily — fields are parsed only when accessed
- supports zero‑copy access to string values
- keeps the underlying buffer alive automatically

---

## **Usage**

```ruby
doc = JSON.load_lazy("data.json")

doc["user"]["name"]   # parsed on demand
doc.array_each do |item|
  puts item["id"]
end
```

---

## **How it works**

`JSON.load_lazy(path)` performs:

1. **Load file into simdjson::padded_string**
   This ensures correct padding and optimal memory layout.

2. **Wrap it in a Ruby `JSON::PaddedString`**
   This object owns the buffer and ensures lifetime safety.

3. **Create a `JSON::PaddedStringView`**
   A lightweight view into the padded buffer.

4. **Create a `JSON::Parser`**
   If none is provided.

5. **Create a `JSON::Document`**
   Bound to the view and parser.

The result is a fully lazy, streaming JSON document.

---

## **Example: Streaming a Large File**

```ruby
doc = JSON.load_lazy("big.json")

doc.array_each do |record|
  puts record["id"]
end
```

This avoids building a giant Ruby array and keeps memory usage minimal.

---

## **With a Reusable Parser**

You can reuse a parser across multiple files:

```ruby
parser = JSON::Parser.new

doc1 = JSON.load_lazy("file1.json", parser)
doc2 = JSON.load_lazy("file2.json", parser)
```

This reduces allocations and improves throughput.

---

## **Error Handling**

All simdjson errors are mapped to Ruby exceptions:


Lookup misses return `nil`:

```ruby
doc["missing"]   # => nil
```

But strict methods raise:

```ruby
doc.fetch("missing")   # => KeyError
```

---

## **Integration with native_ext_deserialize**

Lazy documents can be deserialized directly into Ruby objects:

```ruby
class User
  attr_reader :id, :name
  native_ext_deserialize :@id,   JSON::Type::Number
  native_ext_deserialize :@name, JSON::Type::String
end

doc = JSON.load_lazy("user.json")
user = User.new
doc.into(user)
```

This avoids building intermediate Ruby hashes entirely.

---

## **When to Use `load_lazy`**

Use it when:

- You’re loading large JSON files
- You want streaming access
- You want minimal memory overhead
- You want to deserialize directly into Ruby objects
- You want simdjson’s full performance without DOM overhead

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

## Requirements
You need at least a C++20 compatible compiler.

## **License**

Apache-2.0