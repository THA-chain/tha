import requests
import json
import time

class THARpcClient:
    def __init__(self, rpc_user, rpc_password, wallet_name, rpc_host='127.0.0.1', rpc_port=18332):
        self.url = f'http://{rpc_host}:{rpc_port}/wallet/{wallet_name}'
        self.headers = {'content-type': 'application/json'}
        self.auth = (rpc_user, rpc_password)

    def call(self, method, params=[]):
        payload = json.dumps({"method": method, "params": params, "jsonrpc": "2.0", "id": 0})
        response = requests.post(self.url, headers=self.headers, auth=self.auth, data=payload)
        if response.status_code == 200:
            return response.json()['result']
        else:
            raise Exception(f"Error calling RPC method {method}: {response.status_code} - {response.text}")

    def list_transactions(self, count=10, skip=0, include_watch_only=True):
        return self.call('listtransactions', ["*", count, skip, include_watch_only])

    def get_transaction(self, txid):
        return self.call('gettransaction', [txid])

if __name__ == '__main__':
    # Prompt for user input
    rpc_user = input('Enter RPC username: ')
    rpc_password = input('Enter RPC password: ')
    wallet_name = 'SEND'

    client = THARpcClient(rpc_user, rpc_password, wallet_name)

    try:
        print("Listening for new transactions...")
        while True:
            transactions = client.list_transactions()
            for tx in transactions:
                if tx['category'] == 'receive' and tx['confirmations'] >= 1:
                    print(f"Transaction {tx['txid']} confirmed. Proceeding to next steps.")
                    # Here, trigger the next steps or exit loop
                    # Example: os.system('python3 path_to_next_script.py')
            time.sleep(60)  # Wait for a minute before checking again
    except Exception as e:
        print(f"An error occurred: {e}")
