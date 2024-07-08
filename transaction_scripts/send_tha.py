import requests
import json

class THARpcClient:
    def __init__(self, rpc_user, rpc_password, wallet_path, rpc_host='127.0.0.1', rpc_port=18332):
        self.url = f'http://{rpc_host}:{rpc_port}/wallet/{wallet_path}'
        self.headers = {'content-type': 'application/json'}
        self.auth = (rpc_user, rpc_password)

    def call(self, method, params=[]):
        payload = json.dumps({"method": method, "params": params, "jsonrpc": "2.0", "id": 0})
        response = requests.post(self.url, headers=self.headers, auth=self.auth, data=payload)
        if response.status_code == 200:
            return response.json()['result']
        else:
            raise Exception(f"Error calling RPC method {method}: {response.status_code} - {response.text}")

    def send_to_address(self, address, amount):
        return self.call('sendtoaddress', [address, amount])

if __name__ == '__main__':
    rpc_user = 'nucash'
    rpc_password = 'x'
    wallet_path = 'SEND'  # This should be adjusted if the wallet name is different or needs special handling.
    recipient_address = '1BSxJvD1iGv9UYHFU1uTVvxD7L4kT6W58s'
    amount = 0.1

    client = THARpcClient(rpc_user, rpc_password, wallet_path)

    try:
        # Send 0.1 THA to the recipient address
        txid = client.send_to_address(recipient_address, amount)
        print(f"Transaction ID: {txid}")

    except Exception as e:
        print(f"An error occurred: {e}")
