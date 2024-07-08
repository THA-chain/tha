import requests
import json
import time

class THARpcClient:
    def __init__(self, rpc_user, rpc_password, rpc_host='127.0.0.1', rpc_port=18332):
        # Construct the URL for the RPC connection
        self.url = f'http://{rpc_host}:{rpc_port}/'
        self.headers = {'content-type': 'application/json'}
        self.auth = (rpc_user, rpc_password)

    def call(self, method, params=[]):
        # Prepare the payload for the RPC call
        payload = json.dumps({"method": method, "params": params, "jsonrpc": "2.0", "id": 0})
        # Send the RPC request
        response = requests.post(self.url, headers=self.headers, auth=self.auth, data=payload)
        if response.status_code == 200:
            return response.json()['result']
        else:
            raise Exception(f"Error calling RPC method {method}: {response.status_code} - {response.text}")

    def get_blockchain_info(self):
        # Call the 'getblockchaininfo' RPC method
        return self.call('getblockchaininfo')

if __name__ == '__main__':
    rpc_user = 'nucash'
    rpc_password = 'x'
    client = THARpcClient(rpc_user, rpc_password)

    try:
        # Fetch and print blockchain information
        blockchain_info = client.get_blockchain_info()
        print(json.dumps(blockchain_info, indent=4))
    except Exception as e:
        print(f"An error occurred: {e}")
