//
// Created by serizba on 29/6/20.
//

#ifndef CPPFLOW2_MODEL_H
#define CPPFLOW2_MODEL_H

#include <tensorflow/c/c_api.h>
#include <string>
#include <fstream>
#include <iostream>
#include <vector>

#include "context.h"
#include "tensor.h"

namespace cppflow {

    class model {
    public:
        explicit model(const std::string& filename);

        std::vector<std::string> get_operations() const;

        std::vector<tensor> operator()(std::vector<std::tuple<std::string, tensor>> inputs, std::vector<std::string> outputs);
        tensor operator()(const tensor& input);

    private:

        TF_Graph* graph;
        TF_Session* session;
    };
}

namespace cppflow {

    model::model(const std::string &filename) {
        this->graph = TF_NewGraph();

        // Create the session.
        TF_SessionOptions* session_options = TF_NewSessionOptions();
        TF_Buffer* run_options = TF_NewBufferFromString("", 0);
        TF_Buffer* meta_graph = TF_NewBuffer();

        int tag_len = 1;
        const char* tag = "serve";
        this->session = TF_LoadSessionFromSavedModel(session_options, run_options, filename.c_str(), &tag, tag_len, graph, meta_graph, context::get_status());
        TF_DeleteSessionOptions(session_options);
        TF_DeleteBuffer(run_options);
        //TF_DeleteBuffer(meta_graph);

        status_check(context::get_status());
    }

    std::vector<std::string> model::get_operations() const {
        std::vector<std::string> result;
        size_t pos = 0;
        TF_Operation* oper;

        // Iterate through the operations of a graph
        while ((oper = TF_GraphNextOperation(this->graph, &pos)) != nullptr) {
            result.emplace_back(TF_OperationName(oper));
        }
        return result;
    }

    std::tuple<std::string, int> parse_name(const std::string& name) {
        auto idx = name.find(':');
        return (idx == -1 ? std::make_tuple(name, 0) : std::make_tuple(name.substr(0, idx), std::stoi(name.substr(idx + 1))));
    }

    std::vector<tensor> model::operator()(std::vector<std::tuple<std::string, tensor>> inputs, std::vector<std::string> outputs) {

        std::vector<TF_Output> inp_ops(inputs.size());
        std::vector<TF_Tensor*> inp_val(inputs.size());
        for (int i=0; i<inputs.size(); i++) {

            // Operations
            const auto[op_name, op_idx] = parse_name(std::get<0>(inputs[i]));
            inp_ops[i].oper = TF_GraphOperationByName(this->graph, op_name.c_str());
            inp_ops[i].index = op_idx;

            if (!inp_ops[i].oper)
                throw std::runtime_error("No operation named \"" + op_name + "\" exists");

            // Values
            auto inp_tensor = TFE_TensorHandleResolve(std::get<1>(inputs[i]).tfe_handle.get(), context::get_status());
            status_check(context::get_status());
            inp_val[i] = inp_tensor;
        }

        std::vector<TF_Output> out_ops(outputs.size());
        auto out_val = std::make_unique<TF_Tensor*[]>(outputs.size());
        for (int i=0; i<outputs.size(); i++) {

            const auto[op_name, op_idx] = parse_name(outputs[i]);
            out_ops[i].oper = TF_GraphOperationByName(this->graph, op_name.c_str());
            out_ops[i].index = op_idx;

            if (!out_ops[i].oper)
                throw std::runtime_error("No operation named \"" + op_name + "\" exists");

        }

        TF_SessionRun(this->session, NULL,
                inp_ops.data(), inp_val.data(), inputs.size(),
                out_ops.data(), out_val.get(), outputs.size(),
                NULL, 0,NULL , context::get_status());
        status_check(context::get_status());

        std::vector<tensor> result;
        result.reserve(outputs.size());
        for (int i=0; i<outputs.size(); i++) {
            result.emplace_back(tensor(out_val[i]));
        }

        return result;
    }

    tensor model::operator()(const tensor& input) {
        return (*this)({{"serving_default_input_1", input}}, {"StatefulPartitionedCall"})[0];
    }
}

#endif //CPPFLOW2_MODEL_H