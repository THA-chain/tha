# THA Mining and Transaction Automation Scripts

This repository contains a collection of scripts designed to automate various tasks related to THA cryptocurrency operations, such as monitoring wallets for new block rewards and automating transactions.

## Folder Structure

- `mining_scripts/`: Contains scripts that handle the detection and forwarding of mining rewards.
- `transaction_scripts/`: Contains scripts designed to automate the sending of multiple transactions.

## Scripts Description

### Mining Scripts

- **MineRewardRedirector.py**
  - **Purpose**: Monitors the RECEIVE wallet for new block rewards and forwards them to a specific address.
  - **Usage**:
    ```bash
    python3 mining_scripts/MineRewardRedirector.py
    ```

- **BlockRewardConfirmationTrigger.py**
  - **Purpose**: Listens for incoming transactions of newly mined coins to the SEND wallet and triggers a batch transaction process.
  - **Usage**:
    ```bash
    python3 mining_scripts/BlockRewardConfirmationTrigger.py
    ```

### Transaction Scripts

- **SendTransactions.py**
  - **Purpose**: Sends 500 transactions over 10 minutes.
  - **Usage**:
    ```bash
    python3 transaction_scripts/SendTransactions.py
    ```

## Setup

1. Ensure Python 3.x is installed on your system.
2. Install required Python packages:
   ```bash
   pip install requests
