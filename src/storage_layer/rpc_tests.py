from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
rpc_user = 'test'
rpc_password = 'testpw'
rpc_port = '19932'
rpc_connection_string = 'http://'+rpc_user+':'+rpc_password+'@127.0.0.1:'+rpc_port
rpc_connection = AuthServiceProxy(rpc_connection_string)
best_block_hash = rpc_connection.getbestblockhash()
print(rpc_connection.getblock(best_block_hash))


# batch support : print timestamps of blocks 0 to 99 in 2 RPC round-trips:
commands = [ [ "getblockhash", height] for height in range(100) ]
block_hashes = rpc_connection.batch_(commands)
blocks = rpc_connection.batch_([ [ "getblock", h ] for h in block_hashes ])
block_times = [ block["time"] for block in blocks ]
print(block_times)