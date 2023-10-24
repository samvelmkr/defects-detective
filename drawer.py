import sys
import pydot

graph = pydot.Dot(graph_type='digraph')

if len(sys.argv) != 3:
    print("Usage: python drwaer.py input.txt")
    sys.exit(1)

with open(sys.argv[1], 'r') as file:
    data = file.readlines()

for line in data:
    if not line:
        break
    source, destination = line.strip().split('-->')
    source = source.strip()
    destination = destination.strip()

    sourceNode = pydot.Node(source, label=source,
                            style="filled", shape="rect")
    graph.add_node(sourceNode)
    destinationNode = pydot.Node(destination, label=destination,
                                 style="filled", shape="rect")
    graph.add_node(sourceNode)

    edge = pydot.Edge(sourceNode, destinationNode)
    graph.add_edge(edge)

graph.write_dot(sys.argv[2])

print("Graph created")
