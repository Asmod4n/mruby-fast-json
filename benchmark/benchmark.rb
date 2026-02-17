json_size = File.size("output.json")
$json = File.read("output.json")

def measure_json_performance(json_size)
  parse_ops = 0
  dump_ops  = 0
  parse_bytes = 0
  dump_bytes  = 0

  puts "Parse start: #{Time.now}"
  parse_timer = Chrono::Timer.new
  obj = JSON.parse($json)
  parse_elapsed = parse_timer.elapsed
  parse_bytes += json_size
  parse_ops += 1

  puts "Dump start: #{Time.now}"
  dump_timer = Chrono::Timer.new
  dumped_json = JSON.dump(obj)
  dump_elapsed = dump_timer.elapsed
  dump_bytes += dumped_json.bytesize
  dump_ops += 1

  {
    parse: {
      ops: parse_ops,
      gbps: (parse_bytes.to_f / parse_elapsed) / 1_000_000_000,
      ops_per_sec: (parse_ops.to_f / parse_elapsed),
      time: parse_elapsed
    },
    dump: {
      ops: dump_ops,
      gbps: (dump_bytes.to_f / dump_elapsed) / 1_000_000_000,
      ops_per_sec: (dump_ops.to_f / dump_elapsed),
      time: dump_elapsed
    }
  }
end

result = measure_json_performance(json_size)

puts "--- Parse ---"
puts "Performance        : #{result[:parse][:gbps].round(2)} GBps"
puts "Ops/sec            : #{result[:parse][:ops_per_sec].round(2)}"
puts "Elapsed            : #{result[:parse][:time].round(6)} seconds"

puts "--- Dump ---"
puts "Performance        : #{result[:dump][:gbps].round(2)} GBps"
puts "Ops/sec            : #{result[:dump][:ops_per_sec].round(2)}"
puts "Elapsed            : #{result[:dump][:time].round(6)} seconds"
