var answer = ak.include('test_app', 'answer.js');

function checkSpotRequest() {
  return (ak._request('another_app', '') ==
          ('{"user":"","arg":"","data":null,"file_contents":[],"issuer":' +
           '{"name":"test_app","spot":' +
           '{"name":"test_spot","owner":"test_user"}}}'));
}

