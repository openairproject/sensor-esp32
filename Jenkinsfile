pipeline {
    agent any
    stages {
        stage('build') {
            steps {
            	checkout scm
                sh 'rm -rf build'
                sh 'bin/make_default.sh'
            }
        }
        stage('test') {
            steps {
                sh 'bin/make_tests.sh'
                sh 'sleep 3'
                sh 'bin/run_tests.py /opt/oap/dev/ttyOAP.TEST'
            }
        }
        stage('archive') {
        	steps {
        		sh 'cat build/sensor-esp32.bin | openssl dgst -sha256 > build/sensor-esp32.bin.sha256'
        	}
	        post {
	            success {
	        	   archiveArtifacts artifacts: 'build/sensor-esp32.*', fingerprint: true
	        	   archiveArtifacts artifacts: 'build/partitions.bin', fingerprint: true
	        	   archiveArtifacts artifacts: 'build/bootloader/bootloader.bin', fingerprint: true
	            }
	        }
        }
    }
}