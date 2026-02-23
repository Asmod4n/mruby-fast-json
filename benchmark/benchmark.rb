json_size = File.size("output.json")
$json = File.read('output.json')
JSON.zero_copy_parsing = true

def measure_json_dump_performance(json_size)
  # --- Load (bis doc fertig ist) ---
  puts "Parse start: #{Time.now}"
  load_timer = Chrono::Timer.new
  parser = JSON::Parser.new(json_size)
  parser.allocate(json_size)
  doc = parser.iterate($json)
  load_elapsed = load_timer.elapsed

  # --- Parse / access (erste Nutzung) ---
  puts "Access start: #{Time.now}"
  parse_timer = Chrono::Timer.new
  data = doc.array_each
  parse_elapsed = parse_timer.elapsed
  mrb_raise "returned nil" if data.nil?

  # --- Dump repeatedly for 1 second ---
  puts "Dump start: #{Time.now}"
  dump_ops = 0
  dump_bytes = 0
  dump_timer = Chrono::Timer.new

  while dump_timer.elapsed < 1.0
    dumped_json = JSON.dump(data)
    dump_ops += 1
    dump_bytes += dumped_json.bytesize
  end

  dump_elapsed = dump_timer.elapsed

  {
    load: {
      time: load_elapsed,
      gbps: (json_size.to_f / load_elapsed) / 1_000_000_000,
      ops_per_sec: 1.0 / load_elapsed
    },
    parse: {
      time: parse_elapsed,
      gbps: (json_size.to_f / parse_elapsed) / 1_000_000_000,
      ops_per_sec: 1.0 / parse_elapsed
    },
    dump: {
      ops: dump_ops,
      gbps: (dump_bytes.to_f / dump_elapsed) / 1_000_000_000,
      ops_per_sec: dump_ops.to_f / dump_elapsed,
      time: dump_elapsed
    }
  }
end

result = measure_json_dump_performance(json_size)

puts "--- Load (JSON.parse_lazy) ---"
puts "Elapsed            : #{result[:load][:time].round(6)} seconds"
puts "Throughput         : #{result[:load][:gbps].round(2)} GBps"
puts "Ops/sec (1 load)   : #{result[:load][:ops_per_sec].round(2)}"

puts "--- Access (first access) ---"
puts "Elapsed            : #{result[:parse][:time].round(6)} seconds"
puts "Throughput         : #{result[:parse][:gbps].round(2)} GBps"
puts "Ops/sec (1 parse)  : #{result[:parse][:ops_per_sec].round(2)}"

puts "--- Dump (1 second sustained) ---"
puts "Performance        : #{result[:dump][:gbps].round(2)} GBps"
puts "Ops/sec            : #{result[:dump][:ops_per_sec].round(2)}"
puts "Elapsed            : #{result[:dump][:time].round(6)} seconds"
