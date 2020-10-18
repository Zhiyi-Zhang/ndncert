#! /bin/bash

# Generate ndncert config file
echo 'Please enter the /ndn certificate:(end with Ctrl-D)'
ROOT_CERT=$(cat | tr -d '\n')

cat > ndncert-site-client.conf << ~EOF
{
  "ca-list":
  [
    {
      "ca-prefix": "/ndn",
      "ca-info": "NDN Testbed Root Trust Anchor",
      "max-validity-period": "1296000",
      "max-suffix-length": "3",
      "probe-parameters":
      [
        {"probe-parameter-key": "pin"}
      ],
      "certificate": "$ROOT_CERT"
    }
  ]
}
~EOF

echo 'Please enter the /ndn certificate:(end with Ctrl-D)'
ROOT_CERT=$(cat | tr -d '\n')

# compile and install ndncert
git clone https://github.com/Zhiyi-Zhang/ndncert.git
cd ndncert
git checkout origin/v0.3
./waf configure
sudo ./waf install
sudo cp ./build/systemd/ndncert-ca.service /etc/systemd/system/
sudo chmod 644 /etc/systemd/system/ndncert-ca.service



# prepare CA configuration file
echo -e "{\n\"ca-prefix\": \"/ndn\",\n\"ca-info\": \"NDN testbed root CA\",\n\"max-validity-period\": \"1296000\",\n\"max-suffix-length\": \"2\",\n\"supported-challenges\":\n[\n{ \"challenge\": \"pin\" }\n]\n}" > /usr/local/etc/ndncert/ca.conf

# run the CA
sudo systemctl start ndncert-ca
sleep(2)

# check the status to make sure everything is correct
sudo systemctl status ndncert-server
