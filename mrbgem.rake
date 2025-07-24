MRuby::Gem::Specification.new('mruby-fast-json') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'simdjson for mruby'

  simdjson_src = File.expand_path("#{spec.dir}/deps/simdjson/singleheader", __dir__)

  spec.cxx.include_paths << simdjson_src
  source_files = %W(
    #{simdjson_src}/simdjson.cpp
  )
  spec.objs += source_files.map { |f| f.relative_path_from(dir).pathmap("#{build_dir}/%X#{spec.exts.object}" ) }
end
