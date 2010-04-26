pass = (
  require('answer').answer == 42 &&
  module.app == 'test-app' &&
  module.owner == 'test user' &&
  module.spot == 'test-spot');
