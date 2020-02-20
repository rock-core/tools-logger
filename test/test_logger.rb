require 'minitest/autorun'
require 'orocos/test/component'

require 'pocolog'
require 'fileutils'

class TC_BasicBehaviour < Minitest::Test
    include Orocos::Test::Component
    start 'logger', 'rock_logger'

    def task
        logger
    end

    def setup
        super
        task.overwrite_existing_files = false
        task.auto_rename_existing_files = true
        task.file = logfile_path
    end

    def teardown
        path = task.file
        super
        if !path.empty?
            FileUtils.rm_f(path.gsub(/\.idx$/, ".log"))
        end
        if @logfile_io
            @logfile_io.close
        end
        if @logfile
            @logfile.close
        end
    end

    def logfile_path(arg='/tmp/rock_logger_test.log')
        @logfile_io ||= File.open(arg, "w+")
        unless @logfile_io.path == arg
          @logfile_io.close
          @logfile_io = File.open(arg, "w+")
        end
        @logfile_io.path
    end

    def logfile(file)
        @logfile ||= Pocolog::Logfiles.open(logfile_path(file))
    end

    def generate_and_check_logfile
        assert(task.has_port?('time'))

        task.configure
        task.start

        writer = task.time.writer(:type => :buffer, :size => 2000)
        expected = []
        10.times do |i|
            expected << Time.at(i)
            writer.write(expected.last)
        end
        sleep 0.1

        # Consider possible changes of _file property before flushing
        if task.auto_rename_existing_files
            logfile_path(task.file)
        end
        # Make sure the I/O is properly flushed
        task.stop
        task.cleanup

        stream = logfile(task.file).stream('time')
        samples = stream.samples.to_a.map(&:last)
        assert_equal(expected, samples)
        stream
    end

    def test_basics
        assert(!task.has_port?('time'))
        assert(task.createLoggingPort('time', '/base/Time', []))
        generate_and_check_logfile
    end

    def test_metadata
        assert(!task.has_port?('time'))
        meta = []
        meta << Hash['key' => 'key0', 'value' => 'value0']
        meta << Hash['key' => 'key1', 'value' => 'value1']
        assert(task.createLoggingPort('time', '/base/Time', meta))
        stream = generate_and_check_logfile
        assert_equal({'key0' => 'value0', 'key1' => 'value1', 'rock_cxx_type_name' => '/base/Time'}, stream.metadata)
    end

    def test_create_port_log
        source = new_ruby_task_context 'source'
        source.create_output_port 'out', '/int32_t'
        task.log(source.out)
        assert(task.has_port?('source.out'))
        task.configure
        task.start
        if task.auto_rename_existing_files
            logfile_path(task.file)
        end
        task.stop

        stream = logfile(task.file).stream('source.out')
        expected_metadata = {
            'rock_stream_type' => 'port',
            'rock_task_model' => nil,
            'rock_task_name' => 'source',
            'rock_task_object_name' => 'out',
            'rock_orocos_type_name' => '/int32_t',
            'rock_cxx_type_name' => '/int32_t'
        }
        assert_equal expected_metadata, stream.metadata
    end

    def test_log_port
        # Create a ruby task as source
        source = new_ruby_task_context 'source'
        source.create_output_port 'out', '/int'
        task.log(source.out)
        task.configure
        task.start

        source.out.write 1
        source.out.write 2
        source.out.write 3
        sleep 0.1

        if task.auto_rename_existing_files
            logfile_path(task.file)
        end
        task.stop
        stream = logfile(task.file).stream('source.out')
        assert_equal [1, 2, 3], stream.samples.to_a.map(&:last)
    end

    def test_create_property_log
        source = new_ruby_task_context 'source'
        source.create_property 'file', '/std/string'
        task.create_log(source.property('file'))
        assert(task.has_port?('source.file'))
        task.configure
        task.start
        if task.auto_rename_existing_files
            logfile_path(task.file)
        end
        task.stop

        stream = logfile(task.file).stream('source.file')
        expected_metadata = {
            'rock_stream_type' => 'property',
            'rock_task_model' => nil,
            'rock_task_name' => 'source',
            'rock_task_object_name' => 'file',
            'rock_orocos_type_name' => '/std/string',
            'rock_cxx_type_name' => '/std/string'
        }
        assert_equal expected_metadata, stream.metadata
    end

    def test_log_property
        source = new_ruby_task_context 'source'
        source.create_property 'file', '/std/string'
        source.file = "test"
        task.log(source.property('file'))
        task.configure
        task.start

        source.file = "bla.0.log"
        sleep 0.1

        if task.auto_rename_existing_files
            logfile_path(task.file)
        end
        task.stop

        stream = logfile(task.file).stream('source.file')
        assert_equal ["test", "bla.0.log"], stream.samples.to_a.map(&:last)
    end
end
