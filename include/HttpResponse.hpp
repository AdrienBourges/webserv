/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpResponse.hpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/11/28 by you                       #+#    #+#             */
/*   Updated: 2025/11/28 by you                       ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTPRESPONSE_HPP
# define HTTPRESPONSE_HPP

# include <string>
# include <map>

/*
    HttpResponse

    Représente une réponse HTTP complète que l'on peut sérialiser
    en string brute à envoyer sur la socket.

    Contient :
      - un code de statut (200, 404, 500, ...)
      - une raison textuelle ("OK", "Not Found", ...)
      - des headers (clé:valeur)
      - un body (string)

    toString() produit quelque chose comme :

      HTTP/1.1 200 OK\r\n
      Header1: value\r\n
      Header2: value\r\n
      Server: webserv/0.1\r\n
      Content-Length: <taille body>\r\n
      \r\n
      <body>
*/

class HttpResponse
{
public:
	HttpResponse();
	~HttpResponse();

	void setStatus(int code, const std::string &reason);
	void setBody(const std::string &body);
	void setHeader(const std::string &name, const std::string &value);

	int getStatusCode() const;

	// Construit la string brute à envoyer sur le réseau.
	std::string toString() const;

private:
	int _statusCode;
	std::string _reasonPhrase;
	std::map<std::string, std::string> _headers;
	std::string _body;
};

#endif // HTTPRESPONSE_HPP

