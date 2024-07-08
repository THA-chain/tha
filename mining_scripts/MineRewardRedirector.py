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

    def list_transactions(self, count, skip=0):
        return self.call('listtransactions', ["*", count, skip, True])

    def get_transaction(self, txid):
        return self.call('gettransaction', [txid])

    def send_to_address(self, address, amount):
        return self.call('sendtoaddress', [address, amount])

if __name__ == '__main__':
    # Prompt for user input
    rpc_user = input('Enter RPC username: ')
    rpc_password = input('Enter RPC password: ')
    wallet_name = 'RECEIVE'
    send_address = '1HXJUf7zwHZ5K2vhf9fMueMkXzjMUgDv6k'
    client = THARpcClient(rpc_user, rpc_password, wallet_name)

    try:
        while True:
            transactions = client.list_transactions(10)
            for tx in transactions:
                if tx['category'] == 'receive' and tx['confirmations'] == 1:
                    txid = tx['txid']
                    transaction = client.get_transaction(txid)
                    if transaction['details'][0]['category'] == 'generate':
                        amount = transaction['amount']
                        send_txid = client.send_to_address(send_address, amount)
                        print(f"Sent {amount} THA back to {send_address}. Transaction ID: {send_txid}")
            time.sleep(60)
    except Exception as e:
        print(f"An error occurred: {e}")
