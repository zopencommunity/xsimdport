node('linux') {
  stage ('Poll') {
    checkout([
      $class: 'GitSCM', branches: [[name: '*/main']], extensions: [],
      userRemoteConfigs: [[url: 'https://github.com/zopencommunity/xsimdport.git']]])
  }
  stage('Build') {
    build job: 'Port-Pipeline', parameters: [
      string(name: 'PORT_GITHUB_REPO', value: 'https://github.com/zopencommunity/xsimdport.git'),
      string(name: 'PORT_DESCRIPTION', value: 'C++ wrappers for SIMD intrinsics'),
      string(name: 'BUILD_LINE', value: 'STABLE')
    ]
  }
}
